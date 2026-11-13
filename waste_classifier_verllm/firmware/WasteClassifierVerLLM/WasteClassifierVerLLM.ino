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
#include <new>
#include <Chirale_TensorFlowLite.h>
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "model_data.h"
#include "preprocess.h"
#include "llm_config.h"
#include "wifi_config.h"
#include "web_page.h"

// A vision LLM behind an OpenAI-compatible chat-completions endpoint. Providers are
// tried in array order within one shared watchdog budget (LLM_WATCHDOG_MS).
struct LlmProvider {
  const char *name, *chat_url, *probe_url, *key, *model;
  const char *options;          // Provider-specific request fields (JSON fragment)
  const char *group;            // Keys of the same service share a group
  bool configured = false;      // Key and model set in llm_config.h
  bool online = false;          // Boot probe succeeded
  uint32_t backoff_until = 0;   // millis() until which a 429 says to stay away
  volatile bool busy = false;   // A request (possibly abandoned by the watchdog) is in flight
};
LlmProvider llm_providers[] = {
  {"groq", GROQ_CHAT_URL, GROQ_PROBE_URL, GROQ_API_KEY, GROQ_MODEL, GROQ_REQUEST_OPTIONS, "groq"},
  {"gemini", GEMINI_CHAT_URL, GEMINI_PROBE_URL, GEMINI_API_KEY, GEMINI_MODEL, GEMINI_REQUEST_OPTIONS, "gemini"},
  {"gemini2", GEMINI_CHAT_URL, GEMINI_PROBE_URL, GEMINI_API_KEY_2, GEMINI_MODEL, GEMINI_REQUEST_OPTIONS, "gemini"},
  {"gemini3", GEMINI_CHAT_URL, GEMINI_PROBE_URL, GEMINI_API_KEY_3, GEMINI_MODEL, GEMINI_REQUEST_OPTIONS, "gemini"},
};
constexpr size_t LLM_PROVIDER_COUNT = sizeof(llm_providers) / sizeof(llm_providers[0]);
// Round-robin start per group, so keys share the quota instead of key 1 taking
// every request until it hits 429.
uint32_t llm_rr = 0;

constexpr size_t LLM_MIN_FREE_INTERNAL = 64 * 1024;  // Internal RAM needed to open another TLS session

// One LLM request handed to its own task. If the watchdog in askLlm fires, the job is
// marked abandoned and the task frees it when the request eventually returns; each
// provider has at most one request in flight (LlmProvider::busy).
struct LlmJob {
  LlmProvider *provider = nullptr;
  char *payload = nullptr;
  size_t len = 0;
  int status = -1;
  long retry_after = 0;
  String body;
  bool done = false, abandoned = false;
  SemaphoreHandle_t finished = nullptr;
};
portMUX_TYPE llm_mux = portMUX_INITIALIZER_UNLOCKED;

bool llmUsable(const LlmProvider &p) {
  bool backedOff = p.backoff_until && (int32_t)(p.backoff_until - millis()) > 0;
  return p.configured && p.online && !p.busy && !backedOff;
}
bool anyLlmUsable() {
  for (const LlmProvider &p : llm_providers) if (llmUsable(p)) return true;
  return false;
}

// Groups keep array order; providers inside a group are rotated by llm_rr.
size_t llmOrder(LlmProvider *order[]) {
  size_t n = 0;
  for (size_t i = 0; i < LLM_PROVIDER_COUNT; ++i) {
    const char *g = llm_providers[i].group;
    bool seen = false;
    for (size_t k = 0; k < i; ++k) seen |= strcmp(llm_providers[k].group, g) == 0;
    if (seen) continue;
    LlmProvider *members[LLM_PROVIDER_COUNT]; size_t m = 0;
    for (size_t k = i; k < LLM_PROVIDER_COUNT; ++k)
      if (strcmp(llm_providers[k].group, g) == 0) members[m++] = &llm_providers[k];
    for (size_t k = 0; k < m; ++k) order[n++] = members[(llm_rr + k) % m];
  }
  return n;
}

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
// ESP-NOW channel discovery, see ESP_NOW_BROADCAST_GUIDE.md. The controller hops
// channels 1-13 (200 ms each) until it hears us, then locks and answers with WACK.
// We stay on WIFI_CHANNEL and beacon "PING" there so it can find us.
struct __attribute__((packed)) ControllerAckPacket {
  char magic[6];       // "WACK"
  uint8_t channel;     // Channel the controller locked to
  uint8_t status;      // 1 = idle, 2 = sorting
  uint32_t message_id; // Echo of capture_id (0 for PING)
};
constexpr uint32_t ESPNOW_BEACON_SCAN_MS = 150;   // Shorter than the 200 ms dwell, so a hop never misses us
constexpr uint32_t ESPNOW_BEACON_LINKED_MS = 1000; // Keepalive, well under the controller's 60 s timeout
constexpr uint32_t ESPNOW_LINK_TIMEOUT_MS = 3000;  // No WACK for this long = controller is scanning
constexpr uint32_t ESPNOW_RESEND_MS = 150;
constexpr uint32_t ESPNOW_RESEND_WINDOW_MS = 3000; // > one full 13x200 ms scan cycle
volatile uint32_t espnow_last_ack_ms = 0;
volatile uint32_t espnow_last_ack_id = 0;
volatile uint8_t espnow_ack_channel = 0, espnow_ack_status = 0;
volatile bool espnow_ack_seen = false;  // Controller speaks the WACK protocol
uint8_t espnow_controller_mac[6] = {};
WastePacket espnow_pending = {};        // Label waiting for its WACK
uint32_t espnow_pending_since = 0, espnow_pending_last = 0;
bool espnow_pending_active = false;
bool camera_ok = false, model_ok = false;
String model_error = "Classifier not initialized";
String last_json;
// Every capture attempt (web, button, auto) bumps attempt_id so the web UI can show
// results of captures it did not start itself, e.g. the physical button.
uint32_t attempt_id = 0;
bool last_attempt_ok = false;
const char *last_trigger = "";

// ── Physical capture button + WS2812 status LED ─────────────────
// GPIO 4/5 are the camera SCCB bus; do not reuse them here.
constexpr int BTN_PIN = 14;              // Active LOW: button to GND, internal pull-up
constexpr int LED_PIN = 3;               // DIN of the WS2812B chain
constexpr int LED_COUNT = 3;             // All pixels always show the same color
constexpr uint32_t BTN_DEBOUNCE_MS = 50;
constexpr uint32_t LED_LABEL_MS = 3000;  // Red hold after a label is received
// Green  = waiting for a capture
// Yellow = image captured, waiting for the label
// Red    = label received (held LED_LABEL_MS), then back to green
struct LedColor { uint8_t r, g, b; };
constexpr LedColor LED_RED    = {255, 0, 0};
constexpr LedColor LED_GREEN  = {0, 255, 0};
constexpr LedColor LED_YELLOW = {255, 255, 0};
volatile bool led_analyzing = false;
volatile uint32_t led_label_until = 0;

// Writes one GRB frame to every pixel. rgbLedWrite() only drives the first pixel.
void ledShow(uint8_t r, uint8_t g, uint8_t b) {
  static bool ready = false;
  if (!ready) ready = rmtInit(LED_PIN, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000);  // 100 ns tick
  if (!ready) return;
  uint8_t grb[3] = {g, r, b};  // WS2812 wire order
  static rmt_data_t bits[24 * LED_COUNT];
  int i = 0;
  for (int led = 0; led < LED_COUNT; ++led)
    for (int c = 0; c < 3; ++c)
      for (int b = 7; b >= 0; --b) {
        bool one = grb[c] & (1 << b);
        bits[i].level0 = 1; bits[i].duration0 = one ? 8 : 4;
        bits[i].level1 = 0; bits[i].duration1 = one ? 4 : 8;
        ++i;
      }
  rmtWrite(LED_PIN, bits, 24 * LED_COUNT, RMT_WAIT_FOR_EVER);
}

// Runs on its own task so the LED keeps updating while setup() or a Groq request blocks.
void ledTask(void *) {
  const LedColor *shown = nullptr;
  for (;;) {
    uint32_t until = led_label_until;
    bool labelShown = until && (int32_t)(until - millis()) > 0;
    const LedColor *color = led_analyzing ? &LED_YELLOW : labelShown ? &LED_RED : &LED_GREEN;
    if (color != shown) { shown = color; ledShow(color->r, color->g, color->b); }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

bool useLocalModel() {
  return tflite_ok && !anyLlmUsable();
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
  backend_ok = false;
  for (LlmProvider &p : llm_providers) {
    p.configured = strlen(p.key) > 20 && strlen(p.model) > 0;
    Serial.printf("%s API %s\n", p.name, p.configured ? "configured" : "key not set; skipped");
    backend_ok |= p.configured;
  }
  if (!backend_ok && !tflite_ok) {
    if (!model_error.length()) model_error = "Set a Groq or Gemini key in llm_config.h";
    return false;
  }
  if (!backend_ok) Serial.println("No LLM key set; only the on-device model is available");
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

bool llmBegin(HTTPClient &http, WiFiClientSecure &client, const char *url, const char *key);

// ── LLM requests run on their own task, guarded by a watchdog in askLlm ──
// LlmJob / LlmProvider are declared at the top of the file (Arduino's generated
// prototypes need them).
void freeLlmJob(LlmJob *job) {
  if (job->finished) vSemaphoreDelete(job->finished);
  heap_caps_free(job->payload);
  job->provider->busy = false;
  delete job;
}

// Separate function so the HTTP/TLS objects are destroyed before vTaskDelete().
void llmRequest(LlmJob *job) {
  WiFiClientSecure client;
  HTTPClient http;
  const char *retryHeaders[] = {"Retry-After"};
  if (llmBegin(http, client, job->provider->chat_url, job->provider->key)) {
    http.addHeader("Content-Type", "application/json");
    http.collectHeaders(retryHeaders, 1);
    job->status = http.POST((uint8_t *)job->payload, job->len);
  }
  heap_caps_free(job->payload);
  job->payload = nullptr;
  if (job->status == 429) job->retry_after = http.header("Retry-After").toInt();
  // Error bodies are kept too, so the reason can be printed on Serial.
  else if (job->status > 0 && http.getSize() <= LLM_MAX_RESPONSE_BYTES) job->body = http.getString();
  http.end();
}

void llmTask(void *arg) {
  LlmJob *job = (LlmJob *)arg;
  llmRequest(job);
  portENTER_CRITICAL(&llm_mux);
  bool abandoned = job->abandoned;
  job->done = true;
  portEXIT_CRITICAL(&llm_mux);
  if (abandoned) {
    Serial.printf("%s: late reply ignored (status=%d)\n", job->provider->name, job->status);
    freeLlmJob(job);
  } else {
    xSemaphoreGive(job->finished);
  }
  vTaskDelete(nullptr);
}

// Asks one provider, waiting at most budgetMs (0 = no limit). On success *label is
// "plastic", "metal" or "paper". On failure error/httpStatus describe why.
bool askLlm(LlmProvider &p, const char *b64, size_t b64Len, uint32_t budgetMs,
            const char **label, uint32_t &invokeMs, int &httpStatus, String &error) {
  static const char MESSAGES[] =
    "\",\"messages\":["
    "{\"role\":\"system\",\"content\":\"Look at the nearest object. Pick exactly one label. "
    "plastic: bottle, cup, box, bag, or cap; dull or see-through. "
    "metal: can or tin; shiny silver or colored metal. "
    "paper: sheet, carton, or wrapper; matte, fibrous, or printed. "
    "Blur and odd color are normal. Never answer unknown. "
    "JSON key label, value plastic, metal, or paper.\"},"
    "{\"role\":\"user\",\"content\":["
    "{\"type\":\"text\",\"text\":\"Nearest object only. plastic, metal, or paper?\"},"
    "{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64,";
  static const char TAIL[] = "\"}}]}],\"response_format\":{\"type\":\"json_object\"},";
  httpStatus = 502;
  // Two TLS sessions can be alive at once (an abandoned request plus this one).
  if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < LLM_MIN_FREE_INTERNAL) {
    Serial.printf("%s: skipped, internal RAM low (%u bytes)\n", p.name, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    error = String(p.name) + ": not enough RAM for TLS";
    return false;
  }
  const char *parts[] = {"{\"model\":\"", p.model, MESSAGES, b64, TAIL, p.options, "}"};
  size_t lens[] = {10, strlen(p.model), sizeof(MESSAGES) - 1, b64Len, sizeof(TAIL) - 1, strlen(p.options), 1};
  size_t total = 0;
  for (size_t len : lens) total += len;
  char *payload = (char *)heap_caps_malloc(total + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!payload) { error = "Payload allocation failed"; return false; }
  size_t at = 0;
  for (size_t i = 0; i < 7; ++i) { memcpy(payload + at, parts[i], lens[i]); at += lens[i]; }
  payload[total] = 0;

  LlmJob *job = new (std::nothrow) LlmJob();
  SemaphoreHandle_t finished = job ? xSemaphoreCreateBinary() : nullptr;
  if (!job || !finished) {
    if (finished) vSemaphoreDelete(finished);
    delete job; heap_caps_free(payload);
    error = "LLM job allocation failed"; return false;
  }
  job->provider = &p; job->payload = payload; job->len = total; job->finished = finished;
  uint32_t invokeStart = millis();
  p.busy = true;
  if (xTaskCreatePinnedToCore(llmTask, "LlmRequest", 16384, job, 1, nullptr, 1) != pdPASS) {
    freeLlmJob(job);
    error = "LLM task could not start"; return false;
  }
  TickType_t wait = budgetMs ? pdMS_TO_TICKS(budgetMs) : portMAX_DELAY;
  if (xSemaphoreTake(finished, wait) != pdTRUE) {
    portENTER_CRITICAL(&llm_mux);
    bool done = job->done;
    if (!done) job->abandoned = true; // The task frees the job when it finally returns.
    portEXIT_CRITICAL(&llm_mux);
    if (!done) {
      Serial.printf("%s watchdog: no answer within %lu ms\n", p.name, (unsigned long)budgetMs);
      httpStatus = 504; error = String(p.name) + " timed out";
      return false;
    }
    xSemaphoreTake(finished, portMAX_DELAY); // Finished right at the deadline; give is imminent.
  }
  int status = job->status;
  long retryAfter = job->retry_after;
  String body = std::move(job->body);
  freeLlmJob(job);
  invokeMs = millis() - invokeStart;
  Serial.printf("%s HTTP status=%d latency_ms=%lu\n", p.name, status, (unsigned long)invokeMs);

  if (status == 429) {
    long seconds = std::max<long>(LLM_RATE_LIMIT_BACKOFF_MS / 1000, std::min<long>(86400, retryAfter ? retryAfter : 60));
    p.backoff_until = millis() + (uint32_t)seconds * 1000;
    Serial.printf("%s rate limit; skipping it for %ld seconds\n", p.name, seconds);
    httpStatus = 429; error = String(p.name) + " rate limited";
    return false;
  }
  if (status != 200) {
    Serial.printf("%s request failed: %.300s\n", p.name, body.c_str());
    httpStatus = status > 0 ? status : 502; error = String(p.name) + " request failed";
    return false;
  }
  error = String("Invalid ") + p.name + " response";
  if (!body.length() || body.length() > LLM_MAX_RESPONSE_BYTES) return false;
  JsonDocument response;
  if (deserializeJson(response, body)) return false;
  const char *content = response["choices"][0]["message"]["content"];
  const char *finish = response["choices"][0]["finish_reason"];
  if (!content || !finish || strcmp(finish, "stop") != 0) {
    Serial.printf("%s: incomplete reply (finish_reason=%s)\n", p.name, finish ? finish : "none");
    return false;
  }
  const char *jsonStart = strchr(content, '{');
  const char *jsonEnd = strrchr(content, '}');
  JsonDocument inner;
  if (!jsonStart || !jsonEnd || jsonEnd < jsonStart || deserializeJson(inner, jsonStart, jsonEnd - jsonStart + 1)) {
    Serial.printf("%s: label JSON missing\n", p.name);
    return false;
  }
  const char *answer = inner["label"];
  static const char *ACCEPTED[] = {"plastic", "metal", "paper"};
  for (const char *candidate : ACCEPTED) {
    if (answer && strcmp(answer, candidate) == 0) { *label = candidate; httpStatus = 200; error = ""; return true; }
  }
  Serial.printf("%s label \"%s\" is not plastic, metal, or paper\n", p.name, answer ? answer : "");
  error = "Unsupported classification label";
  return false;
}

bool runClassify(String &jsonOut, String &error, int &httpStatus) {
  httpStatus=503;
  jsonOut="";
  last_json=""; // Never expose a previous classification after this attempt fails.
  if(!camera_ok || !model_ok){error=camera_ok?model_error:"Camera initialization failed";return false;}
  if(api_link==ApiChecking){error="Dang ket noi toi server";return false;}
  // LLMs first on every capture (Groq, then Gemini). Local only when no provider is
  // usable right now: none reachable at boot, all in 429 back-off, or all still busy
  // with a request the watchdog abandoned.
  bool localWindow=useLocalModel();
  if(!localWindow && WiFi.status()!=WL_CONNECTED){
    if(tflite_ok) localWindow=true;
    else {error="Wi-Fi disconnected; no servo command sent";return false;}
  }
  if(!localWindow && !backend_ok){error="Set a Groq or Gemini key in llm_config.h";return false;}
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
  if(localWindow){
    if(tflite_ok)Serial.println("No LLM provider usable right now; using on-device model");
    return runLocal(jsonOut,error,httpStatus,started);
  }
  size_t b64Max=4*((last_size+2)/3);
  char *b64=(char*)heap_caps_malloc(b64Max+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  size_t olen=0;
  if(!b64 || mbedtls_base64_encode((unsigned char*)b64,b64Max+1,&olen,last_jpeg,last_size)!=0){
    heap_caps_free(b64);return runLocal(jsonOut,error,httpStatus,started);
  }
  last_llm_attempt_ms=millis();llm_attempted=true;llm_cooldown_ms=LLM_CAPTURE_INTERVAL_MS;
  // One watchdog budget shared by all providers. A provider that has another usable
  // one after it gets at most LLM_PRIMARY_BUDGET_MS so the next one still has time.
  uint32_t deadline=millis()+LLM_WATCHDOG_MS;
  LlmProvider *order[LLM_PROVIDER_COUNT];
  size_t count=llmOrder(order);
  ++llm_rr;
  // Only a 429 moves on to another key of the same service. A timeout or 5xx means
  // the service itself is slow, and its other keys hit the same servers.
  const char *skipGroup=nullptr;
  for(size_t i=0;i<count;++i){
    LlmProvider &p=*order[i];
    if(skipGroup && strcmp(p.group,skipGroup)==0)continue;
    // Without an on-device fallback, try anything configured rather than give up.
    bool usable=tflite_ok?llmUsable(p):(p.configured && !p.busy);
    if(!usable){
      if(p.configured && p.busy)Serial.printf("%s: previous request still running; skipped\n",p.name);
      continue;
    }
    uint32_t budget=0;
    if(tflite_ok){
      int32_t left=(int32_t)(deadline-millis());
      if(left<(int32_t)LLM_MIN_ATTEMPT_MS){Serial.println("LLM watchdog budget used up");break;}
      budget=(uint32_t)left;
      // Cap only when a different service is still left to try.
      for(size_t j=i+1;j<count;++j){
        if(strcmp(order[j]->group,p.group)!=0 && llmUsable(*order[j])){budget=std::min<uint32_t>(budget,LLM_PRIMARY_BUDGET_MS);break;}
      }
    }
    const char *label=nullptr;uint32_t invoke_ms=0;
    if(!askLlm(p,b64,olen,budget,&label,invoke_ms,httpStatus,error)){
      if(httpStatus!=429){skipGroup=p.group;Serial.printf("%s failed (%d); skipping other %s keys\n",p.name,httpStatus,p.group);}
      continue;
    }
    heap_caps_free(b64);
    // Existing UI + Esp32Core use paper_cardboard. Preserve that wire protocol.
    const char *wireLabel=strcmp(label,"paper")==0?"paper_cardboard":label;
    JsonDocument result;
    result["capture_id"]=capture_id;
    result["status"]="accepted";
    result["label"]=wireLabel;
    result["predicted_label"]=wireLabel;
    result["llm_label"]=label;
    result["source"]="api";
    result["provider"]=p.name;
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
  heap_caps_free(b64);
  if(tflite_ok){
    Serial.println("No LLM answer; using on-device model");
    return runLocal(jsonOut,error,httpStatus,started);
  }
  if(!error.length())error="No LLM provider answered and on-device model is unavailable";
  return false;
}

// WiFi task context: only copy fields, no Serial or blocking calls.
void onEspNowRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != (int)sizeof(ControllerAckPacket) || memcmp(data, "WACK", 4) != 0) return;
  ControllerAckPacket ack;
  memcpy(&ack, data, sizeof(ack));
  memcpy(espnow_controller_mac, info->src_addr, 6);
  espnow_ack_channel = ack.channel;
  espnow_ack_status = ack.status;
  espnow_last_ack_id = ack.message_id;
  espnow_last_ack_ms = millis();
  espnow_ack_seen = true;
}

bool espNowLinked() {
  uint32_t t = espnow_last_ack_ms;
  return espnow_ack_seen && millis() - t < ESPNOW_LINK_TIMEOUT_MS;
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
  esp_now_register_recv_cb(onEspNowRecv);
  espnow_ready = true;
  Serial.printf("ESP-NOW ready on channel %d, MAC %s\n", WiFi.channel(), WiFi.softAPmacAddress().c_str());
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
  Serial.printf("ESP-NOW %s to broadcast on ch %d: %s%s\n", label, WiFi.channel(), esp_err_to_name(err),
                espNowLinked() ? "" : " (controller not linked)");
  // Only a controller that has ACKed before gets resends; an older one has no
  // WACK, so resending would make it sort the same item again.
  if (espnow_ack_seen) {
    espnow_pending = packet;
    espnow_pending_since = espnow_pending_last = millis();
    espnow_pending_active = true;
  }
}

// Call from loop(): PING beacon for discovery, resend the last label until WACK.
// Everything stays on WIFI_CHANNEL. Sweeping other channels (guide pattern A) would
// move the hotspot, and a controller catching a swept copy would lock onto a
// channel the camera is not on.
void serviceEspNow() {
  if (!espnow_ready) return;
  uint32_t now = millis();
  static bool was_linked = false;
  bool linked = espNowLinked();
  if (linked != was_linked) {
    was_linked = linked;
    if (linked) {
      const uint8_t *m = espnow_controller_mac;
      Serial.printf("ESP-NOW controller linked: %02X:%02X:%02X:%02X:%02X:%02X ch %u status %u\n",
                    m[0], m[1], m[2], m[3], m[4], m[5], espnow_ack_channel, espnow_ack_status);
    } else {
      Serial.println("ESP-NOW controller lost, beaconing fast");
    }
  }

  if (espnow_pending_active) {
    if (espnow_last_ack_id == espnow_pending.capture_id && (int32_t)(espnow_last_ack_ms - espnow_pending_since) >= 0) {
      Serial.printf("ESP-NOW %s #%u acknowledged\n", espnow_pending.label, (unsigned)espnow_pending.capture_id);
      espnow_pending_active = false;
    } else if (now - espnow_pending_since >= ESPNOW_RESEND_WINDOW_MS) {
      Serial.printf("ESP-NOW %s #%u not acknowledged, giving up\n", espnow_pending.label, (unsigned)espnow_pending.capture_id);
      espnow_pending_active = false;
    } else if (now - espnow_pending_last >= ESPNOW_RESEND_MS) {
      espnow_pending_last = now;
      esp_now_send(espnow_broadcast, reinterpret_cast<uint8_t *>(&espnow_pending), sizeof(espnow_pending));
    }
    return;  // The resends already let a scanning controller find us
  }

  static uint32_t last_beacon = 0;
  if (now - last_beacon < (linked ? ESPNOW_BEACON_LINKED_MS : ESPNOW_BEACON_SCAN_MS)) return;
  last_beacon = now;
  WastePacket ping = {};
  memcpy(ping.magic, "WASTE", 5);
  strncpy(ping.label, "PING", sizeof(ping.label) - 1);
  esp_now_send(espnow_broadcast, reinterpret_cast<uint8_t *>(&ping), sizeof(ping));
}

void handleCommand() {
  String body = server.arg("plain");
  Serial.printf("Command: %s\n", body.c_str());
  if (body.indexOf("pause") >= 0) auto_enabled = false;
  else if (body.indexOf("resume") >= 0) auto_enabled = true;
  server.send(200, "application/json", String("{\"auto_enabled\":") + (auto_enabled ? "true" : "false") + "}");
}

// Every capture path (web UI, auto mode, button) goes through here so the LED stays in sync.
bool classifyWithLed(String &json, String &error, int &httpStatus, const char *trigger) {
  led_analyzing = true;
  bool ok = runClassify(json, error, httpStatus);
  ++attempt_id; last_attempt_ok = ok; last_trigger = trigger;
  if (ok) led_label_until = millis() + LED_LABEL_MS;  // Failure: no label, straight back to green
  led_analyzing = false;
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
  if (!classifyWithLed(json, error, httpStatus, "button")) Serial.printf("Button capture failed: %s\n", error.c_str());
  // Presses made while analyzing are dropped, not queued.
  stable = last = digitalRead(BTN_PIN);
  changed = millis();
}

void classify() {
  String json, error; int httpStatus=503;
  bool ok=classifyWithLed(json, error, httpStatus, "web");
  server.sendHeader("X-Attempt-Id",String(attempt_id));
  if(!ok){
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
// Channel setting from the portal (NVS "chan"): 0 = follow the router's channel,
// 1-13 = stay on that channel and drop a router on any other one.
uint8_t chanSetting = WIFI_CHANNEL;
// Channel the hotspot uses when it runs without a router.
uint8_t hotspotChannel = WIFI_CHANNEL;

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
  body += "\",\"channel\":";
  body += WiFi.channel();
  body += ",\"chan_setting\":";
  body += chanSetting;
  body += '}';
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
    body += ",\"ch\":";
    body += WiFi.channel(i);
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
  long chan = server.hasArg("channel") ? server.arg("channel").toInt() : chanSetting;
  if (chan < 0 || chan > 13) {
    sendNote(400, "Chưa lưu được", "Kênh phải là Tự động hoặc từ 1 đến 13.");
    return;
  }
  wifiPrefs.begin("waste-wifi", false);
  wifiPrefs.putString("ssid", ssid);
  wifiPrefs.putString("pass", password);
  wifiPrefs.putUChar("chan", (uint8_t)chan);
  wifiPrefs.end();
  server.send_P(200, "text/html; charset=utf-8", SAVED_PAGE);
  delay(800);
  ESP.restart();
}

void holdHotspot() {
  // One radio. A station that keeps scanning pulls the hotspot off its channel
  // and phones stop seeing WasteCamera. Drop the station and stay on WIFI_CHANNEL.
  // A failed station connect leaves the radio on the router's channel, and
  // softAP() skips esp_wifi_set_config() when the config looks unchanged, so the
  // hotspot would stay there. Turn Wi-Fi fully off to force a fresh config.
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD, hotspotChannel)) {
    Serial.println("ERROR: Could not start WasteCamera hotspot");
    return;
  }
  if (WiFi.channel() != hotspotChannel) {
    Serial.printf("Hotspot came up on channel %d, forcing %u\n", WiFi.channel(), hotspotChannel);
    esp_wifi_set_channel(hotspotChannel, WIFI_SECOND_CHAN_NONE);
  }
  Serial.print("Hotspot: "); Serial.println(AP_SSID);
  Serial.print("Setup page: http://"); Serial.print(WiFi.softAPIP());
  Serial.println("/wifi");
}

void startWifi() {
  wifiPrefs.begin("waste-wifi", true);
  savedSSID = wifiPrefs.getString("ssid", "");
  savedPassword = wifiPrefs.getString("pass", "");
  chanSetting = wifiPrefs.getUChar("chan", WIFI_CHANNEL);
  wifiPrefs.end();
  if (chanSetting > 13) chanSetting = WIFI_CHANNEL;
  hotspotChannel = chanSetting ? chanSetting : WIFI_CHANNEL;

  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD, hotspotChannel)) {
    Serial.println("ERROR: Could not start WasteCamera hotspot");
  }

  if (savedSSID.isEmpty()) {
    holdHotspot();
    initEspNow();
    return;
  }

  if (chanSetting) Serial.printf("Connecting to saved Wi-Fi: %s (channel %u only)\n", savedSSID.c_str(), chanSetting);
  else Serial.printf("Connecting to saved Wi-Fi: %s (following its channel)\n", savedSSID.c_str());
  // The channel argument is only a scan hint; the station still joins the router
  // on whatever channel it is. The check below drops a router on another channel.
  if (chanSetting) WiFi.begin(savedSSID.c_str(), savedPassword.c_str(), chanSetting);
  else WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED && chanSetting && WiFi.channel() != chanSetting) {
    Serial.printf("Router %s is on channel %d, not %u. Dropping it to keep WasteCamera on channel %u.\n",
                  savedSSID.c_str(), WiFi.channel(), chanSetting, chanSetting);
    holdHotspot();
  } else if (WiFi.status() == WL_CONNECTED) {
    WiFi.setAutoReconnect(true);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    Serial.print("Wi-Fi connected. Camera URL: http://");
    Serial.println(WiFi.localIP());
    Serial.printf("Channel %d%s\n", WiFi.channel(), chanSetting ? "" : " (following router)");
    Serial.print("Hotspot remains at http://");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.printf("Wi-Fi connection failed, status=%d. Keeping WasteCamera on channel %u.\n", (int)WiFi.status(), hotspotChannel);
    holdHotspot();
  }
  initEspNow();
}

// Auto-reconnect can rejoin the router after it moved to another channel, which
// drags the hotspot and ESP-NOW with it. Drop the router if that happens, unless
// the portal is set to follow the router (the controller rescans to find us).
void enforceChannel() {
  if (!chanSetting) return;
  static uint32_t last_check = 0;
  if (millis() - last_check < 5000) return;
  last_check = millis();
  if (WiFi.getMode() != WIFI_AP_STA || WiFi.status() != WL_CONNECTED) return;
  if (WiFi.channel() == chanSetting) return;
  Serial.printf("Router moved to channel %d. Dropping it to stay on channel %u.\n", WiFi.channel(), chanSetting);
  esp_now_deinit();
  espnow_ready = false;
  holdHotspot();
  initEspNow();
}

bool llmBegin(HTTPClient &http, WiFiClientSecure &client, const char *url, const char *key) {
  client.setInsecure();
  client.setHandshakeTimeout(15);
  http.setConnectTimeout(LLM_CONNECT_TIMEOUT_MS);
  http.setTimeout(LLM_HTTP_TIMEOUT_MS);
  http.setReuse(false);
  if (!http.begin(client, url)) return false;
  http.addHeader("Authorization", String("Bearer ") + key);
  return true;
}

void probeBackend() {
  bool any = false;
  for (LlmProvider &p : llm_providers) {
    p.online = false;
    if (!p.configured || WiFi.status() != WL_CONNECTED) continue;
    WiFiClientSecure client;
    HTTPClient http;
    if (!llmBegin(http, client, p.probe_url, p.key)) {
      Serial.printf("%s probe could not start\n", p.name);
      continue;
    }
    int status = http.GET();
    http.end();
    p.online = status == 200;
    Serial.printf("%s probe status=%d (%s)\n", p.name, status, p.online ? "reachable" : "unavailable");
    any |= p.online;
  }
  api_link = any ? ApiOnline : ApiOffline;
  if (!any) Serial.println(tflite_ok ? "No LLM API reachable; using on-device model" : "No LLM API reachable and no on-device model");
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
      + ",\"api_online\":" + (anyLlmUsable() ? "true" : "false")
      + ",\"capture_interval_ms\":" + String(AUTO_MS)
      + ",\"attempt_id\":" + String(attempt_id)
      + ",\"last_ok\":" + (last_attempt_ok ? "true" : "false")
      + ",\"last_trigger\":\"" + last_trigger + "\"}";
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
    if(!classifyWithLed(json, error, httpStatus, "auto")) Serial.println(error);
  }
  enforceChannel();
  serviceEspNow();
  delay(2);
}
