#include "espNowComms.h"
#include <esp_wifi.h>

QueueHandle_t wasteQueue = nullptr;

// Camera hotspot BSSID. The camera sends ESP-NOW from its AP interface, so this is
// also the source MAC of its packets. Written only by the scan task.
static uint8_t cameraMac[6] = {0};
static volatile bool cameraLocked = false;
static uint8_t currentChannel = ESPNOW_WIFI_CHANNEL;
static TaskHandle_t scanTaskHandle = nullptr;

static void setChannel(uint8_t channel)
{
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
    currentChannel = channel;
}

/**
 * @brief Scan for CAMERA_AP_SSID. channel = 0 scans every channel, otherwise only that one.
 * @return true if the hotspot was found (bssid/channel filled in).
 */
static bool findCamera(uint8_t channel, uint8_t bssid[6], uint8_t &foundChannel)
{
    int n = WiFi.scanNetworks(false, false, false, 300, channel, CAMERA_AP_SSID);
    int best = -1;
    for (int i = 0; i < n; ++i) {
        if (WiFi.SSID(i) == CAMERA_AP_SSID && (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best))) {
            best = i;
        }
    }
    if (best >= 0) {
        memcpy(bssid, WiFi.BSSID(best), 6);
        foundChannel = (uint8_t)WiFi.channel(best);
    }
    WiFi.scanDelete();
    return best >= 0;
}

static void fullScan()
{
    Serial.printf("[SCAN] Looking for camera hotspot \"%s\"...\n", CAMERA_AP_SSID);
    uint8_t bssid[6];
    uint8_t channel = 0;
    if (findCamera(0, bssid, channel)) {
        cameraLocked = false;  // don't filter against a half-written MAC
        memcpy(cameraMac, bssid, 6);
        setChannel(channel);
        cameraLocked = true;
        Serial.printf("[SCAN] Camera %02X:%02X:%02X:%02X:%02X:%02X found on channel %u. Listening only to it.\n",
                      bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], channel);
    } else {
        cameraLocked = false;
        setChannel(currentChannel);  // a scan may leave the radio elsewhere
        Serial.printf("[SCAN] Camera not found. Staying on channel %u, accepting any sender. Retry in %d s.\n",
                      currentChannel, CAMERA_SCAN_RETRY_MS / 1000);
    }
}

/**
 * @brief Keeps the controller on the camera's channel. While the camera is unknown it
 *        rescans every CAMERA_SCAN_RETRY_MS. Once locked it only checks the current channel
 *        (~300 ms off-air) every CAMERA_CHECK_MS, and does a full rescan if the camera moved
 *        (e.g. it rebooted and joined a router on another channel).
 */
static void scanTask(void *)
{
    for (;;) {
        bool rescanRequested = ulTaskNotifyTake(pdTRUE,
            pdMS_TO_TICKS(cameraLocked ? CAMERA_CHECK_MS : CAMERA_SCAN_RETRY_MS)) > 0;
        if (rescanRequested || !cameraLocked) {
            fullScan();
            continue;
        }
        uint8_t bssid[6];
        uint8_t channel = 0;
        bool stillHere = findCamera(currentChannel, bssid, channel) && memcmp(bssid, cameraMac, 6) == 0;
        setChannel(currentChannel);
        if (!stillHere) {
            Serial.println("[SCAN] Camera no longer on this channel, rescanning...");
            fullScan();
        }
    }
}

/**
 * @brief Runs in the Wi-Fi task (not an ISR): never block, use xQueueSend with 0 timeout.
 * Accepts the camera's 34-byte packet, the legacy 20-byte WasteSortPacket, or a raw string.
 */
static void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int data_len)
{
    if (data_len <= 0 || wasteQueue == nullptr) return;
    if (cameraLocked && memcmp(info->src_addr, cameraMac, 6) != 0) return;  // another group's camera

    WasteSortPacket packet;
    memset(&packet, 0, sizeof(packet));

    if (data_len == (int)sizeof(CameraPacket) && memcmp(data, "WASTE", 6) == 0) {
        CameraPacket cam;
        memcpy(&cam, data, sizeof(cam));
        memcpy(packet.tag, cam.label, MAX_TAG_LEN - 1);
        packet.messageId = cam.capture_id;
    } else if (data_len == (int)sizeof(WasteSortPacket)) {
        memcpy(&packet, data, sizeof(WasteSortPacket));
        packet.tag[MAX_TAG_LEN - 1] = '\0';
    } else {
        int copyLen = (data_len < (int)(MAX_TAG_LEN - 1)) ? data_len : (int)(MAX_TAG_LEN - 1);
        memcpy(packet.tag, data, copyLen);
    }

    xQueueSend(wasteQueue, &packet, 0);
}

void espNowRequestRescan()
{
    if (scanTaskHandle) xTaskNotifyGive(scanTaskHandle);
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

    // 2. Station mode, not connected to any router; ESP-NOW only needs the radio
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    Serial.print("[INIT] Device ESP-NOW MAC Address: ");
    Serial.println(WiFi.macAddress());

    // 3. Find the camera and tune to its channel before listening
    fullScan();

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

    // 6. Track the camera's channel in the background (core 0, away from the servo task)
    xTaskCreatePinnedToCore(scanTask, "CamScan", 4096, nullptr, 1, &scanTaskHandle, 0);

    Serial.println("[INIT] ESP-NOW listening for camera packets");
    return true;
}
