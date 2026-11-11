#include "espNowComms.h"
#include <esp_wifi.h>

QueueHandle_t wasteQueue = nullptr;

/**
 * @brief High-priority callback executed when an ESP-NOW packet arrives.
 * 
 * NOTE: This runs in WiFi driver task context. Never block or delay here.
 * We immediately push the packet into a FreeRTOS Queue and wake the consumer task.
 */
static void onDataRecv(const uint8_t *mac_addr, const uint8_t *data, int data_len)
{
    if (data_len <= 0 || wasteQueue == nullptr) return;

    WasteSortPacket packet;
    memset(&packet, 0, sizeof(packet));

    if (data_len == sizeof(CameraWastePacket) ||
        (data_len >= 6 && memcmp(data, "WASTE", 5) == 0 && data_len >= (int)sizeof(CameraWastePacket))) {
        CameraWastePacket camPacket;
        memcpy(&camPacket, data, sizeof(CameraWastePacket));
        camPacket.label[MAX_TAG_LEN - 1] = '\0';
        strncpy(packet.tag, camPacket.label, sizeof(packet.tag) - 1);
        packet.messageId = camPacket.capture_id;
    } else if (data_len == sizeof(WasteSortPacket)) {
        memcpy(&packet, data, sizeof(WasteSortPacket));
        packet.tag[MAX_TAG_LEN - 1] = '\0'; // sender may fill all 16 bytes
    } else {
        // Fallback for raw string payloads from CV board
        int copyLen = (data_len < (int)(MAX_TAG_LEN - 1)) ? data_len : (int)(MAX_TAG_LEN - 1);
        memcpy(packet.tag, data, copyLen);
        packet.tag[copyLen] = '\0';
        packet.messageId = 0;
    }

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

    // 2. Set WiFi station mode and channel
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

    // 3. Print Local MAC Address for pairing
    Serial.print("[INIT] Device ESP-NOW MAC Address: ");
    Serial.println(WiFi.macAddress());

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

    Serial.println("[INIT] ESP-NOW initialized successfully and listening for CV packets");
    return true;
}