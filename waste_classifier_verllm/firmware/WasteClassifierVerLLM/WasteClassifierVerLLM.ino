#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <WebServer.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "esp_camera.h"
#include "img_converters.h"
#include "esp_heap_caps.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>
#include <cmath>
#include <Chirale_TensorFlowLite.h>
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "model_data.h"
#include "preprocess.h"
#include "llm_config.h"
#include "wifi_config.h"
#include "web_page.h"

WebServer server(80);
const char *LABELS[] = {"plastic", "metal", "paper_cardboard"};
constexpr size_t ARENA_BYTES = 4 * 1024 * 1024;
tflite::MicroInterpreter *interpreter = nullptr;
TfLiteTensor *input = nullptr, *output = nullptr;
uint8_t *arena = nullptr, *last_jpeg = nullptr;
size_t last_size = 0;
int last_width = 0, last_height = 0;
uint32_t capture_id = 0;
uint32_t last_auto_ms = 0;
uint32_t last_llm_attempt_ms = 0;
uint32_t llm_cooldown_ms = LLM_CAPTURE_INTERVAL_MS;
uint32_t local_until_ms = 0;
bool llm_attempted = false;
bool tflite_ok = false, backend_ok = false;
enum ApiLink : uint8_t { ApiChecking, ApiOnline, ApiOffline };
ApiLink api_link = ApiChecking;
uint32_t llmWaitMs() {
  uint32_t elapsed = millis() - last_llm_attempt_ms;
  return llm_attempted && elapsed < llm_cooldown_ms ? llm_cooldown_ms - elapsed : 0;
}
constexpr uint32_t AUTO_MS = LLM_CAPTURE_INTERVAL_MS;
bool auto_enabled = false;
bool espnow_ready = false;
uint8_t espnow_broadcast[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
struct __attribute__((packed)) WastePacket {
  char magic[6];
  uint32_t capture_id;
  float score;
  uint32_t inference_ms;
  char label[16];
};
bool camera_ok = false, model_ok = false;
String model_error = "Classifier not initialized";
String last_json;

// ── Physical capture button + WS2812 status LED ─────────────────
// GPIO 4/5 are the camera SCCB bus; do not reuse them here.
constexpr int BTN_PIN = 14;              // Active LOW: button to GND, internal pull-up
constexpr int LED_PIN = 3;               // DIN of the WS2812B module
constexpr uint8_t LED_LEVEL = 60;        // 0-255 brightness per channel
constexpr uint32_t BTN_DEBOUNCE_MS = 50;
constexpr uint32_t LED_ERROR_MS = 2000;  // Fast red blink after a failed analysis
// Booting  = slow red blink   NoWifi = steady red   Ready = green
// Analyzing = yellow           Fault/error = fast red blink
enum LedState : uint8_t { LedBooting, LedReady, LedNoWifi, LedAnalyzing, LedFault };
volatile LedState led_state = LedBooting;
volatile uint32_t led_error_until = 0;

// Runs on its own task so the LED keeps blinking while setup() or a Groq request blocks.
void ledTask(void *) {
  uint32_t shown = 0xFFFFFFFF;
  for (;;) {
    uint32_t now = millis();
    LedState s = led_state;
    uint32_t until = led_error_until;
    bool errorFlash = until && (int32_t)(until - now) > 0;
    uint32_t color;  // 0xRRGGBB
    if (s == LedAnalyzing) color = 0xFFA000;
    else if (errorFlash || s == LedFault) color = (now / 100) % 2 ? 0xFF0000 : 0;
    else if (s == LedBooting) color = (now / 400) % 2 ? 0xFF0000 : 0;
    else if (s == LedNoWifi) color = 0xFF0000;
    else color = 0x00FF00;
    if (color != shown) {
      shown = color;
      rgbLedWrite(LED_PIN, ((color >> 16) & 0xFF) * LED_LEVEL / 255,
                  ((color >> 8) & 0xFF) * LED_LEVEL / 255, (color & 0xFF) * LED_LEVEL / 255);
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

bool useLocalModel() {
  if (!tflite_ok) return false;
  if (api_link == ApiOffline) return true;
  return local_until_ms && (int32_t)(local_until_ms - millis()) > 0;
}

bool initCamera() {
  camera_config_t c = {};
  c.ledc_channel=LEDC_CHANNEL_0; c.ledc_timer=LEDC_TIMER_0;
  c.pin_xclk=15; c.pin_sccb_sda=4; c.pin_sccb_scl=5;
  c.pin_d0=11; c.pin_d1=9; c.pin_d2=8; c.pin_d3=10;
  c.pin_d4=12; c.pin_d5=18; c.pin_d6=17; c.pin_d7=16;
  c.pin_vsync=6; c.pin_href=7; c.pin_pclk=13; c.pin_pwdn=-1; c.pin_reset=-1;
  c.xclk_freq_hz=20000000; c.pixel_format=PIXFORMAT_JPEG;
  c.frame_size=FRAMESIZE_QVGA; c.jpeg_quality=10; c.fb_count=1;
  c.fb_location=CAMERA_FB_IN_PSRAM; c.grab_mode=CAMERA_GRAB_WHEN_EMPTY;
  esp_err_t err=esp_camera_init(&c);
  Serial.printf("Camera init: %s\n",esp_err_to_name(err));
  return err==ESP_OK;
}

bool initLocalModel() {
  const tflite::Model *model = tflite::GetModel(g_model);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    model_error = "TFLite schema mismatch";
    return false;
  }
  static tflite::MicroMutableOpResolver<10> resolver;
  if (resolver.AddConv2D() != kTfLiteOk || resolver.AddDepthwiseConv2D() != kTfLiteOk ||
      resolver.AddAdd() != kTfLiteOk || resolver.AddMean() != kTfLiteOk ||
      resolver.AddFullyConnected() != kTfLiteOk || resolver.AddReshape() != kTfLiteOk ||
      resolver.AddPad() != kTfLiteOk || resolver.AddRelu6() != kTfLiteOk ||
      resolver.AddQuantize() != kTfLiteOk || resolver.AddDequantize() != kTfLiteOk) {
    model_error = "Operator registration failed";
    return false;
  }
  arena = (uint8_t *)heap_caps_aligned_alloc(16, ARENA_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!arena) {
    model_error = "Cannot allocate 4MB arena in PSRAM";
    return false;
  }
  static tflite::MicroInterpreter runtime(model, resolver, arena, ARENA_BYTES);
  interpreter = &runtime;
  if (interpreter->AllocateTensors() != kTfLiteOk) {
    model_error = "AllocateTensors failed; inspect Serial for unsupported op or insufficient arena";
    return false;
  }
  input = interpreter->input(0);
  output = interpreter->output(0);
  if (input->type != kTfLiteInt8 || output->type != kTfLiteInt8 || input->dims->size != 4 ||
      input->dims->data[0] != 1 || input->dims->data[1] != 128 || input->dims->data[2] != 128 ||
      input->dims->data[3] != 3 || output->bytes != 3 || input->params.scale <= 0 || output->params.scale <= 0) {
    model_error = "Expected INT8 NHWC [1,128,128,3] and 3 output logits";
    return false;
  }
  Serial.printf("Local model ready; arena used=%u bytes; free PSRAM=%u\n",
                (unsigned)interpreter->arena_used_bytes(), (unsigned)ESP.getFreePsram());
  return true;
}

// model_ready stays true when either path can classify.
// The URL check is configuration only, not a live backend health check.
bool initModel() {
  tflite_ok = initLocalModel();
  if (!tflite_ok) Serial.println(model_error);
  backend_ok = strlen(GROQ_API_KEY) > 20 && strlen(GROQ_MODEL) > 0;
  if (!backend_ok && !tflite_ok) {
    if (!model_error.length()) model_error = "Set the Groq key in llm_config.h";
    return false;
  }
  if (!backend_ok) Serial.println("Groq key is not set; only the on-device model is available");
  else Serial.println("Groq API configured");
  if (tflite_ok || backend_ok) model_error = "";
  return tflite_ok || backend_ok;
}

camera_fb_t *freshFrame() {
  camera_fb_t *stale=esp_camera_fb_get(); if(stale)esp_camera_fb_return(stale);
  return esp_camera_fb_get();
}

void cors() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void sendOptions() {
  cors();
  server.send(204);
}

void sendJpeg(const uint8_t *data,size_t length) {
  cors();
  server.sendHeader("Cache-Control","no-store");
  server.setContentLength(length);server.send(200,"image/jpeg","");
  WiFiClient client=server.client();
  size_t sent=0;unsigned long last_progress=millis();
  while(sent<length && client.connected() && millis()-last_progress<5000) {
    size_t chunk=std::min<size_t>(1460,length-sent);
    size_t n=client.write(data+sent,chunk);
    if(n){sent+=n;last_progress=millis();} else delay(1);
  }
  if(sent!=length)client.stop();
}

void snapshot() {
  if(!camera_ok){cors();server.send(503,"text/plain","Camera initialization failed; inspect Serial");return;}
  camera_fb_t *f=freshFrame();
  if(!f){server.send(503,"text/plain","Camera capture failed");return;}
  sendJpeg(f->buf,f->len);esp_camera_fb_return(f);
}

bool runLocal(String &jsonOut, String &error, int &httpStatus, uint32_t started) {
  httpStatus=500;
  if(!tflite_ok || !last_jpeg || !last_width || !last_height){error="On-device model is not available";return false;}
  uint8_t *rgb=(uint8_t*)heap_caps_malloc((size_t)last_width*last_height*3,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!rgb || !fmt2rgb888(last_jpeg,last_size,PIXFORMAT_JPEG,rgb)) {
    heap_caps_free(rgb);error="RGB decode/allocation failed";return false;
  }
  const float mean[]={.485f,.456f,.406f},sd[]={.229f,.224f,.225f};
  for(int y=0;y<128;++y)for(int x=0;x<128;++x)for(int c=0;c<3;++c){
    float v=(waste_pixel(rgb,last_width,last_height,128,x,y,c,1)/255.f-mean[c])/sd[c];
    int q=(int)floorf(v/input->params.scale+input->params.zero_point+.5f);
    input->data.int8[(y*128+x)*3+c]=(int8_t)std::max(-128,std::min(127,q));
  }
  heap_caps_free(rgb);
  uint32_t invoke_start=millis();
  if(interpreter->Invoke()!=kTfLiteOk){error="TFLite Invoke failed; inspect Serial";return false;}
  uint32_t invoke_ms=millis()-invoke_start;
  float score[3],maximum=-INFINITY,sum=0;
  for(int i=0;i<3;++i){score[i]=(output->data.int8[i]-output->params.zero_point)*output->params.scale;maximum=std::max(maximum,score[i]);}
  int best=0;for(int i=0;i<3;++i){score[i]=expf(score[i]-maximum);sum+=score[i];}
  for(int i=0;i<3;++i)score[i]/=sum;
  best=0;for(int i=1;i<3;++i)if(score[i]>score[best])best=i;
  char json[640];snprintf(json,sizeof(json),
    "{\"capture_id\":%lu,\"status\":\"accepted\",\"label\":\"%s\",\"predicted_label\":\"%s\",\"source\":\"local\",\"score\":%.6f,\"scores\":[%.6f,%.6f,%.6f],\"inference_ms\":%lu,\"total_ms\":%lu}",
    (unsigned long)capture_id,LABELS[best],LABELS[best],score[best],score[0],score[1],score[2],(unsigned long)invoke_ms,(unsigned long)(millis()-started));
  jsonOut=json;last_json=jsonOut;httpStatus=200;
  Serial.println(jsonOut);
  sendEspNow(LABELS[best],score[best],capture_id,invoke_ms);
  return true;
}

bool runClassify(String &jsonOut, String &error, int &httpStatus) {
  httpStatus=503;
  jsonOut="";
  last_json=""; // Never expose a previous classification after this attempt fails.
  if(!camera_ok || !model_ok){error=camera_ok?model_error:"Camera initialization failed";return false;}
  if(api_link==ApiChecking){error="Dang ket noi toi server";return false;}
  bool localWindow=useLocalModel() || (tflite_ok && llmWaitMs()>0);
  if(!localWindow && WiFi.status()!=WL_CONNECTED){
    if(tflite_ok) localWindow=true;
    else {error="Wi-Fi disconnected; no servo command sent";return false;}
  }
  if(!localWindow && !backend_ok){error="Set the Groq key in llm_config.h";return false;}
  uint32_t started=millis();camera_fb_t *f=freshFrame();
  if(!f){error="Capture failed";return false;}
  if(!f->buf || !f->len || f->format!=PIXFORMAT_JPEG || f->len>LLM_MAX_IMAGE_BYTES) {
    esp_camera_fb_return(f);error="Empty, invalid or oversized JPEG";return false;
  }
  int width=f->width,height=f->height;
  uint8_t *copy=(uint8_t*)heap_caps_malloc(f->len,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!copy){esp_camera_fb_return(f);error="JPEG allocation failed";return false;}
  memcpy(copy,f->buf,f->len);size_t size=f->len;esp_camera_fb_return(f);
  heap_caps_free(last_jpeg);last_jpeg=copy;last_size=size;last_width=width;last_height=height;++capture_id;
  Serial.printf("JPEG bytes=%u capture_id=%lu\n",(unsigned)size,(unsigned long)capture_id);
  if(localWindow)return runLocal(jsonOut,error,httpStatus,started);
  static const char HEAD[] =
    "{\"model\":\"" GROQ_MODEL "\",\"messages\":["
    "{\"role\":\"system\",\"content\":\"Look at the nearest object. Pick exactly one label. "
    "plastic: bottle, cup, box, bag, or cap; dull or see-through. "
    "metal: can or tin; shiny silver or colored metal. "
    "paper: sheet, carton, or wrapper; matte, fibrous, or printed. "
    "Blur and odd color are normal. Never answer unknown. "
    "JSON key label, value plastic, metal, or paper.\"},"
    "{\"role\":\"user\",\"content\":["
    "{\"type\":\"text\",\"text\":\"Nearest object only. plastic, metal, or paper?\"},"
    "{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64,";
  static const char TAIL[] =
    "\"}}]}],\"response_format\":{\"type\":\"json_object\"},"
    "\"reasoning_effort\":\"none\",\"max_completion_tokens\":100}";
  size_t b64Max=4*((last_size+2)/3);
  size_t total=strlen(HEAD)+b64Max+strlen(TAIL);
  char *payload=(char*)heap_caps_malloc(total+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!payload)return runLocal(jsonOut,error,httpStatus,started);
  memcpy(payload,HEAD,strlen(HEAD));
  size_t olen=0;
  int encoded=mbedtls_base64_encode((unsigned char*)payload+strlen(HEAD),b64Max+1,&olen,last_jpeg,last_size);
  if(encoded!=0){heap_caps_free(payload);return runLocal(jsonOut,error,httpStatus,started);}
  memcpy(payload+strlen(HEAD)+olen,TAIL,strlen(TAIL));
  size_t payloadLen=strlen(HEAD)+olen+strlen(TAIL);
  payload[payloadLen]=0;
  last_llm_attempt_ms=millis();llm_attempted=true;llm_cooldown_ms=LLM_CAPTURE_INTERVAL_MS;
  uint32_t invoke_start=millis();
  WiFiClientSecure client;
  HTTPClient http;
  const char *retryHeaders[]={"Retry-After"};
  bool startedRequest=groqBegin(http,client,GROQ_CHAT_URL);
  if(startedRequest){
    http.addHeader("Content-Type","application/json");
    http.collectHeaders(retryHeaders,1);
  }
  int status=startedRequest?http.POST((uint8_t*)payload,payloadLen):-1;
  heap_caps_free(payload);
  uint32_t invoke_ms=millis()-invoke_start;
  Serial.printf("Groq HTTP status=%d latency_ms=%lu\n",status,(unsigned long)invoke_ms);
  if(status==429) {
    long seconds=http.header("Retry-After").toInt();
    http.end();
    seconds=std::max<long>(LLM_RATE_LIMIT_BACKOFF_MS/1000,std::min<long>(86400,seconds?seconds:60));
    local_until_ms=millis()+(uint32_t)seconds*1000;
    llm_attempted=false;
    Serial.printf("Groq rate limit; local model for %ld seconds\n",seconds);
    return runLocal(jsonOut,error,httpStatus,started);
  }
  if(status!=200) {
    http.end();
    Serial.println("Groq request failed; using on-device model");
    if(tflite_ok)return runLocal(jsonOut,error,httpStatus,started);
    httpStatus=status>0?status:502;
    error="Groq request failed and on-device model is unavailable";
    return false;
  }
  if(http.getSize()>LLM_MAX_RESPONSE_BYTES){http.end();return runLocal(jsonOut,error,httpStatus,started);}
  String body=http.getString();
  http.end();
  if(!body.length() || body.length()>LLM_MAX_RESPONSE_BYTES) {
    if(tflite_ok)return runLocal(jsonOut,error,httpStatus,started);
    httpStatus=502;error="Invalid Groq response";return false;
  }
  JsonDocument response;
  if(deserializeJson(response,body)) {
    if(tflite_ok)return runLocal(jsonOut,error,httpStatus,started);
    httpStatus=502;error="Invalid Groq response";return false;
  }
  const char *content=response["choices"][0]["message"]["content"];
  const char *finish=response["choices"][0]["finish_reason"];
  if(!content || !finish || strcmp(finish,"stop")!=0) {
    if(tflite_ok)return runLocal(jsonOut,error,httpStatus,started);
    httpStatus=502;error="Incomplete Groq response";return false;
  }
  const char *jsonStart=strchr(content,'{');
  const char *jsonEnd=strrchr(content,'}');
  JsonDocument inner;
  if(!jsonStart || !jsonEnd || jsonEnd<jsonStart || deserializeJson(inner,jsonStart,jsonEnd-jsonStart+1)) {
    if(tflite_ok)return runLocal(jsonOut,error,httpStatus,started);
    httpStatus=502;error="Groq label JSON missing";return false;
  }
  const char *label=inner["label"];
  if(!label){if(tflite_ok)return runLocal(jsonOut,error,httpStatus,started);httpStatus=502;error="Groq label missing";return false;}
  bool accepted=strcmp(label,"plastic")==0 || strcmp(label,"metal")==0 || strcmp(label,"paper")==0;
  if(!accepted) {
    Serial.printf("Groq label \"%s\" is not plastic, metal, or paper; using on-device model\n", label);
    if(tflite_ok)return runLocal(jsonOut,error,httpStatus,started);
    httpStatus=502;error="Unsupported classification label";return false;
  }
  // Existing UI + Esp32Core use paper_cardboard. Preserve that wire protocol.
  const char *wireLabel=strcmp(label,"paper")==0?"paper_cardboard":label;
  JsonDocument result;
  result["capture_id"]=capture_id;
  result["status"]="accepted";
  result["label"]=wireLabel;
  result["predicted_label"]=wireLabel;
  result["llm_label"]=label;
  result["source"]="api";
  result["score"]=nullptr; // LLM has no calibrated numerical confidence.
  result["scores"]=nullptr;
  result["inference_ms"]=invoke_ms;
  result["total_ms"]=millis()-started;
  serializeJson(result,jsonOut);last_json=jsonOut;httpStatus=200;
  Serial.println(jsonOut);
  // -1 is an unavailable-score sentinel, NOT a confidence value.
  // Existing receiver routes only by label. Unknown/errors send no movement.
  sendEspNow(wireLabel,-1.0f,capture_id,invoke_ms);
  return true;
}

void initEspNow() {
  if (esp_now_init() != ESP_OK) { Serial.println("ESP-NOW init failed"); return; }
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, espnow_broadcast, 6);
  peer.channel = 0;
  // The hotspot is up in every Wi-Fi mode, so sending from it always works (in AP-only
  // mode the STA interface is off and sends would fail with ESP_ERR_ESPNOW_IF). It also
  // makes the sender MAC equal the hotspot BSSID, which the controller locks onto.
  peer.ifidx = WIFI_IF_AP;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) { Serial.println("ESP-NOW broadcast peer failed"); return; }
  espnow_ready = true;
  Serial.printf("ESP-NOW ready on channel %d, MAC %s\n", WiFi.channel(), WiFi.macAddress().c_str());
}

void sendEspNow(const char *label, float score, uint32_t id, uint32_t inferenceMs) {
  if (!espnow_ready) return;
  WastePacket packet = {};
  memcpy(packet.magic, "WASTE", 5);
  packet.capture_id = id;
  packet.score = score;
  packet.inference_ms = inferenceMs;
  strncpy(packet.label, label, sizeof(packet.label) - 1);
  esp_err_t err = esp_now_send(espnow_broadcast, reinterpret_cast<uint8_t *>(&packet), sizeof(packet));
  Serial.printf("ESP-NOW %s to broadcast: %s\n", label, esp_err_to_name(err));
}

void handleCommand() {
  String body = server.arg("plain");
  Serial.printf("Command: %s\n", body.c_str());
  if (body.indexOf("pause") >= 0) auto_enabled = false;
  else if (body.indexOf("resume") >= 0) auto_enabled = true;
  server.send(200, "application/json", String("{\"auto_enabled\":") + (auto_enabled ? "true" : "false") + "}");
}

// Base LED state when nothing is being analyzed. Hotspot-only mode counts as no Wi-Fi.
void updateLed() {
  if (api_link == ApiChecking) led_state = LedBooting;
  else if (!camera_ok || !model_ok) led_state = LedFault;
  else if (WiFi.status() != WL_CONNECTED) led_state = LedNoWifi;
  else led_state = LedReady;
}

// Every capture path (web UI, auto mode, button) goes through here so the LED stays in sync.
bool classifyWithLed(String &json, String &error, int &httpStatus) {
  led_state = LedAnalyzing;
  bool ok = runClassify(json, error, httpStatus);
  if (!ok) led_error_until = millis() + LED_ERROR_MS;
  updateLed();
  return ok;
}

void pollButton() {
  static bool stable = HIGH, last = HIGH;
  static uint32_t changed = 0;
  bool raw = digitalRead(BTN_PIN);
  uint32_t now = millis();
  if (raw != last) { last = raw; changed = now; }
  if (now - changed < BTN_DEBOUNCE_MS || raw == stable) return;
  stable = raw;
  if (stable != LOW) return;
  Serial.println("Button: capture");
  String json, error; int httpStatus = 503;
  if (!classifyWithLed(json, error, httpStatus)) Serial.printf("Button capture failed: %s\n", error.c_str());
  // Presses made while analyzing are dropped, not queued.
  stable = last = digitalRead(BTN_PIN);
  changed = millis();
}

void classify() {
  String json, error; int httpStatus=503;
  if(!classifyWithLed(json, error, httpStatus)){
    cors();
    if(httpStatus==429)server.sendHeader("Retry-After",String((llmWaitMs()+999)/1000));
    server.send(httpStatus,"text/plain",error);return;
  }
  cors();server.sendHeader("Cache-Control","no-store");server.send(200,"application/json",json);
}

// Wi-Fi credentials are stored in NVS, independently of the AI model.
Preferences wifiPrefs;
String savedSSID;
String savedPassword;

String jsonEscape(const String &value) {
  String out;
  out.reserve(value.length() + 4);
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value[i];
    if (c == '"' || c == '\\') out += '\\';
    if ((uint8_t)c < 0x20) continue;
    out += c;
  }
  return out;
}

void sendPage() {
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", PAGE);
}

void sendWifiStatus() {
  bool connected = WiFi.status() == WL_CONNECTED;
  String body = "{\"connected\":";
  body += connected ? "true" : "false";
  body += ",\"ssid\":\"";
  if (connected) body += jsonEscape(WiFi.SSID());
  body += "\",\"ip\":\"";
  if (connected) body += WiFi.localIP().toString();
  body += "\",\"saved\":\"";
  body += jsonEscape(savedSSID);
  body += "\"}";
  cors();
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", body);
}

void sendWifiNetworks() {
  int found = WiFi.scanNetworks();
  String body = "[";
  int shown = 0;
  for (int i = 0; i < found && shown < 15; ++i) {
    String name = WiFi.SSID(i);
    if (!name.length()) continue;
    if (shown) body += ',';
    body += "{\"ssid\":\"";
    body += jsonEscape(name);
    body += "\",\"rssi\":";
    body += WiFi.RSSI(i);
    body += ",\"open\":";
    body += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false";
    body += '}';
    ++shown;
  }
  body += ']';
  WiFi.scanDelete();
  cors();
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", body);
}

void sendNote(int code, const char *heading, const char *text) {
  String page;
  page.reserve(720);
  page += F("<!doctype html><html lang='vi'><meta charset='utf-8'>");
  page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>");
  page += heading;
  page += F("</title><style>body{margin:0;background:#e6efe4;color:#17241c;font:16px/1.45 Segoe UI,sans-serif}");
  page += F("main{max-width:420px;margin:28px auto;background:#fffdf8;border-radius:22px;padding:22px}");
  page += F("a{color:#0e6b52;font-weight:700}</style><main><h1>");
  page += heading;
  page += F("</h1><p>");
  page += text;
  page += F("</p><p><a href='/wifi'>Quay lại cài đặt Wi-Fi</a></p></main></html>");
  server.send(code, "text/html; charset=utf-8", page);
}

void saveWifi() {
  if (!server.hasArg("ssid")) {
    sendNote(400, "Chưa lưu được", "Chưa có tên mạng.");
    return;
  }
  String ssid = server.arg("ssid");
  String password = server.arg("password");
  ssid.trim();
  if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 63 ||
      (!password.isEmpty() && password.length() < 8)) {
    sendNote(400, "Chưa lưu được",
      "Tên mạng tối đa 32 ký tự. Mật khẩu để trống, hoặc dài từ 8 đến 63 ký tự.");
    return;
  }
  wifiPrefs.begin("waste-wifi", false);
  wifiPrefs.putString("ssid", ssid);
  wifiPrefs.putString("pass", password);
  wifiPrefs.end();
  server.send_P(200, "text/html; charset=utf-8", SAVED_PAGE);
  delay(800);
  ESP.restart();
}

void holdHotspot() {
  // One radio. A station that keeps scanning pulls the hotspot off its channel
  // and phones stop seeing WasteCamera. Drop the station and stay on channel 6.
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, true);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD, 6)) {
    Serial.println("ERROR: Could not start WasteCamera hotspot");
    return;
  }
  Serial.print("Hotspot: "); Serial.println(AP_SSID);
  Serial.print("Setup page: http://"); Serial.print(WiFi.softAPIP());
  Serial.println("/wifi");
}

void startWifi() {
  wifiPrefs.begin("waste-wifi", true);
  savedSSID = wifiPrefs.getString("ssid", "");
  savedPassword = wifiPrefs.getString("pass", "");
  wifiPrefs.end();

  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD, 6)) {
    Serial.println("ERROR: Could not start WasteCamera hotspot");
  }

  if (savedSSID.isEmpty()) {
    holdHotspot();
    initEspNow();
    return;
  }

  Serial.printf("Connecting to saved Wi-Fi: %s\n", savedSSID.c_str());
  WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setAutoReconnect(true);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    Serial.print("Wi-Fi connected. Camera URL: http://");
    Serial.println(WiFi.localIP());
    Serial.print("Hotspot remains at http://");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.printf("Wi-Fi connection failed, status=%d. Keeping WasteCamera on channel 6.\n", (int)WiFi.status());
    holdHotspot();
  }
  initEspNow();
}

bool groqBegin(HTTPClient &http, WiFiClientSecure &client, const char *url) {
  client.setInsecure();
  client.setHandshakeTimeout(15);
  http.setConnectTimeout(LLM_CONNECT_TIMEOUT_MS);
  http.setTimeout(LLM_HTTP_TIMEOUT_MS);
  http.setReuse(false);
  if (!http.begin(client, url)) return false;
  http.addHeader("Authorization", String("Bearer ") + GROQ_API_KEY);
  return true;
}

void probeBackend() {
  if (!backend_ok || WiFi.status() != WL_CONNECTED) {
    api_link = ApiOffline;
    Serial.println(tflite_ok ? "Groq unavailable; using on-device model" : "Groq unavailable and no on-device model");
    return;
  }
  WiFiClientSecure client;
  HTTPClient http;
  if (!groqBegin(http, client, GROQ_PROBE_URL)) {
    api_link = ApiOffline;
    Serial.println("Groq probe could not start; using on-device model");
    return;
  }
  int status = http.GET();
  http.end();
  if (status == 200) {
    api_link = ApiOnline;
    Serial.println("Groq API reachable");
    return;
  }
  api_link = ApiOffline;
  Serial.printf("Groq probe status=%d; %s\n", status, tflite_ok ? "using on-device model" : "no on-device model");
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN_PIN, INPUT_PULLUP);
  xTaskCreatePinnedToCore(ledTask, "StatusLed", 3072, nullptr, 1, nullptr, 0);
  delay(300);
  startWifi();
  Serial.printf("PSRAM total=%u free=%u\n",(unsigned)ESP.getPsramSize(),(unsigned)ESP.getFreePsram());
  if(!psramFound()){Serial.println("Enable OPI PSRAM in Arduino Tools before uploading");return;}
  camera_ok=initCamera(); model_ok=initModel();
  if(!model_ok)Serial.println(model_error);
  server.on("/", HTTP_GET, sendPage);
  server.on("/wifi", HTTP_GET, sendPage);
  server.on("/wifi/status", HTTP_GET, sendWifiStatus);
  server.on("/wifi/networks", HTTP_GET, sendWifiNetworks);
  server.on("/wifi/save", HTTP_POST, saveWifi);
  server.on("/capture",HTTP_GET,snapshot);
  server.on("/capture",HTTP_OPTIONS,sendOptions);
  server.on("/classify",HTTP_POST,classify);
  server.on("/classify",HTTP_OPTIONS,sendOptions);
  server.on("/latest",HTTP_GET,[]{
    cors();
    if(!last_json.length()){server.send(204,"application/json","");return;}
    server.sendHeader("Cache-Control","no-store");
    server.send(200,"application/json",last_json);
  });
  server.on("/latest",HTTP_OPTIONS,sendOptions);
  server.on("/last.jpg",HTTP_GET,[]{
    if(!last_jpeg)server.send(404,"text/plain","Classify an image first");
    else if(!server.hasArg("id") || server.arg("id")!=String(capture_id))server.send(409,"text/plain","Image replaced by another capture; classify again");
    else sendJpeg(last_jpeg,last_size);
  });
  server.on("/command", HTTP_POST, handleCommand);
  server.on("/health", HTTP_OPTIONS, sendOptions);
  server.on("/command", HTTP_OPTIONS, sendOptions);
  server.on("/health", HTTP_GET, []() {
    cors();
    String body = String("{\"camera_ready\":") + (camera_ok ? "true" : "false")
      + ",\"model_ready\":" + (model_ok ? "true" : "false")
      + ",\"auto_enabled\":" + (auto_enabled ? "true" : "false")
      + ",\"api_checking\":" + (api_link == ApiChecking ? "true" : "false")
      + ",\"api_online\":" + (api_link == ApiOnline && !useLocalModel() ? "true" : "false")
      + ",\"capture_interval_ms\":" + String(AUTO_MS) + "}";
    server.send(200, "application/json", body);
  });
  server.begin(); Serial.println("Web server ready");
}

void loop() {
  server.handleClient();
  static uint32_t serve_since = 0;
  if(!serve_since) serve_since = millis();
  if(api_link==ApiChecking && millis()-serve_since>400) probeBackend();
  if(api_link!=ApiChecking) pollButton();
  if(api_link!=ApiChecking && auto_enabled && camera_ok && model_ok && millis()-last_auto_ms>=AUTO_MS && llmWaitMs()==0) {
    last_auto_ms=millis();
    String json, error; int httpStatus=503;
    if(!classifyWithLed(json, error, httpStatus)) Serial.println(error);
  }
  updateLed();
  delay(2);
}
