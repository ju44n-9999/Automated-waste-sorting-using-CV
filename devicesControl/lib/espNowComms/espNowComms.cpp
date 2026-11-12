#include "espNowComms.h"
#include <esp_wifi.h>

QueueHandle_t wasteQueue = nullptr;

// ── Channel-lock state ──────────────────────────────────────────
// ESP-NOW only hears frames on the radio's current channel. The camera
// broadcasts from its "WasteCamera" hotspot, which sits on channel 6 when
// standalone but follows the router's channel when the camera joins Wi-Fi.
// So we scan for that SSID and tune to whatever channel it is on.
static volatile uint32_t lastPacketMs = 0;   // written from the WiFi task
static uint32_t lastScanMs = 0;
static uint8_t currentChannel = ESPNOW_WIFI_CHANNEL;
static bool cameraLocked = false;
static bool scanRunning = false;
static bool rescanRequested = false;

static constexpr uint32_t SCAN_MS_PER_CHANNEL = 120;  // beacons are every ~100 ms

static void applyChannel(uint8_t channel)
{
    esp_err_t err = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        Serial.printf("[ERR]  esp_wifi_set_channel(%u) failed: %s\n", channel, esp_err_to_name(err));
        return;
    }
    currentChannel = channel;
}

// Pick the strongest "WasteCamera" entry from the last scan and tune to it.
static void handleScanResult(int16_t count)
{
    uint8_t foundChannel = 0;
    int32_t bestRssi = INT32_MIN;
    String bssid;

    for (int16_t i = 0; i < count; ++i) {
        if (WiFi.SSID(i) == CAMERA_AP_SSID && WiFi.RSSI(i) > bestRssi) {
            bestRssi = WiFi.RSSI(i);
            foundChannel = (uint8_t)WiFi.channel(i);
            bssid = WiFi.BSSIDstr(i);
        }
    }
    WiFi.scanDelete();

    if (foundChannel != 0) {
        if (!cameraLocked || foundChannel != currentChannel) {
            Serial.printf("[ESP-NOW] Camera \"%s\" (%s, %d dBm) found on channel %u\n",
                          CAMERA_AP_SSID, bssid.c_str(), (int)bestRssi, foundChannel);
        }
        cameraLocked = true;
        applyChannel(foundChannel);
    } else {
        if (cameraLocked) {
            Serial.printf("[ESP-NOW] Camera \"%s\" not seen; staying on channel %u\n",
                          CAMERA_AP_SSID, currentChannel);
        }
        cameraLocked = false;
        applyChannel(currentChannel);  // the scan may have left the radio elsewhere
    }
}

/**
 * @brief Callback executed when an ESP-NOW packet arrives.
 *
 * NOTE: This runs in WiFi driver task context. Never block or delay here.
 * We immediately push the packet into a FreeRTOS Queue for the main loop.
 */
#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int data_len)
#else
static void onDataRecv(const uint8_t *mac_addr, const uint8_t *data, int data_len)
#endif
{
    if (data_len <= 0 || wasteQueue == nullptr) return;

    WasteSortPacket packet;
    memset(&packet, 0, sizeof(packet));

    if (data_len >= (int)sizeof(CameraWastePacket) && memcmp(data, "WASTE", 5) == 0) {
        // Camera firmware packet: {"WASTE", capture_id, score, inference_ms, label}
        CameraWastePacket camPacket;
        memcpy(&camPacket, data, sizeof(CameraWastePacket));
        camPacket.label[MAX_TAG_LEN - 1] = '\0';
        strncpy(packet.tag, camPacket.label, sizeof(packet.tag) - 1);
        packet.messageId = camPacket.capture_id;
    } else if (data_len == sizeof(WasteSortPacket)) {
        memcpy(&packet, data, sizeof(WasteSortPacket));
        packet.tag[MAX_TAG_LEN - 1] = '\0'; // sender may fill all 16 bytes
    } else {
        // Fallback for raw string payloads
        int copyLen = (data_len < (int)(MAX_TAG_LEN - 1)) ? data_len : (int)(MAX_TAG_LEN - 1);
        memcpy(packet.tag, data, copyLen);
        packet.tag[copyLen] = '\0';
        packet.messageId = 0;
    }

    lastPacketMs = millis();

    // WiFi task context, not an ISR: use the normal API with zero timeout.
    xQueueSend(wasteQueue, &packet, 0);
}

bool espNowCommsInit()
{
    // 1. Create FreeRTOS Queue for buffer decoupling
    if (wasteQueue == nullptr) {
        wasteQueue = xQueueCreate(WASTE_QUEUE_LENGTH, sizeof(WasteSortPacket));
        if (wasteQueue == nullptr) {
            Serial.println("[ERR]  Failed to create wasteQueue!");
            return false;
        }
    }

    // 2. Station mode, not connected to any AP (ESP-NOW only)
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.setSleep(false);  // modem sleep drops ESP-NOW frames

    Serial.print("[INIT] Device ESP-NOW MAC Address: ");
    Serial.println(WiFi.macAddress());

    // 3. Find the camera's channel (blocking once at boot)
    Serial.printf("[INIT] Scanning for camera hotspot \"%s\"...\n", CAMERA_AP_SSID);
    int16_t count = WiFi.scanNetworks(false, false, false, SCAN_MS_PER_CHANNEL, 0, CAMERA_AP_SSID);
    handleScanResult(count);
    lastScanMs = millis();
    if (!cameraLocked) {
        Serial.printf("[WARN] Camera not found; listening on fallback channel %u, will rescan\n",
                      currentChannel);
    }

    // 4. Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ERR]  ESP-NOW initialization failed!");
        return false;
    }

    // 5. Register receive callback
    esp_err_t err = esp_now_register_recv_cb(onDataRecv);
    if (err != ESP_OK) {
        Serial.printf("[ERR]  Failed to register ESP-NOW recv callback: %d\n", err);
        return false;
    }

    Serial.printf("[INIT] ESP-NOW listening for camera packets on channel %u\n", currentChannel);
    return true;
}

void espNowCommsService()
{
    uint32_t now = millis();

    if (scanRunning) {
        int16_t result = WiFi.scanComplete();
        if (result == WIFI_SCAN_RUNNING) return;
        scanRunning = false;
        lastScanMs = now;
        handleScanResult(result);  // result < 0 (failed) counts as "not found"
        return;
    }

    // Re-check the camera's channel when asked, or when nothing has arrived
    // for a while (camera may have rebooted onto a router's channel).
    bool idle = (now - lastPacketMs >= ESPNOW_RESCAN_IDLE_MS) &&
                (now - lastScanMs >= ESPNOW_RESCAN_IDLE_MS);
    if (!rescanRequested && !idle) return;
    rescanRequested = false;

    int16_t started = WiFi.scanNetworks(true, false, false, SCAN_MS_PER_CHANNEL, 0, CAMERA_AP_SSID);
    if (started == WIFI_SCAN_RUNNING) {
        scanRunning = true;
    } else {
        lastScanMs = now;
    }
}

void espNowCommsRequestRescan()
{
    rescanRequested = true;
}

uint8_t espNowCommsChannel()
{
    return currentChannel;
}
