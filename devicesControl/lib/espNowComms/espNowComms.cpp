#include "espNowComms.h"
#include <esp_wifi.h>

// Standard Arduino state
WasteSortPacket latestPacket;
volatile bool newPacketAvailable = false;

// Simple FIFO ring buffer (pure C++/Arduino, no FreeRTOS) to ensure no packets are dropped during servo motion
static WasteSortPacket packetBuffer[ESPNOW_BUFFER_SIZE];
static volatile uint8_t ringHead = 0;
static volatile uint8_t ringTail = 0;

/**
 * @brief Callback executed when an ESP-NOW packet arrives.
 * 
 * NOTE: This runs in WiFi driver task context. Never block or delay here.
 * We immediately parse the incoming packet, update standard Arduino flags,
 * and buffer the packet for loop() to process.
 */
static void onDataRecv(const uint8_t *mac_addr, const uint8_t *data, int data_len)
{
    if (data_len <= 0) return;

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

    // Update standard Arduino state
    latestPacket = packet;
    newPacketAvailable = true;

    // Buffer incoming packet so it isn't lost during servo actuation
    uint8_t nextHead = (ringHead + 1) % ESPNOW_BUFFER_SIZE;
    if (nextHead != ringTail) {
        packetBuffer[ringHead] = packet;
        ringHead = nextHead;
    }
}

bool espNowAvailable()
{
    return (ringHead != ringTail) || newPacketAvailable;
}

bool espNowRead(WasteSortPacket &packet)
{
    if (ringHead != ringTail) {
        packet = packetBuffer[ringTail];
        ringTail = (ringTail + 1) % ESPNOW_BUFFER_SIZE;
        if (ringHead == ringTail) {
            newPacketAvailable = false;
        }
        return true;
    }

    if (newPacketAvailable) {
        packet = latestPacket;
        newPacketAvailable = false;
        return true;
    }

    return false;
}

bool espNowCommsInit()
{
    // 1. Reset state
    ringHead = 0;
    ringTail = 0;
    newPacketAvailable = false;
    memset(&latestPacket, 0, sizeof(latestPacket));

    // 2. Set WiFi station mode and channel
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

    // 3. Print Local MAC Address and channel for pairing
    Serial.print("[INIT] Device ESP-NOW MAC Address: ");
    Serial.println(WiFi.macAddress());
    Serial.printf("[INIT] ESP-NOW Channel: %d\n", ESPNOW_WIFI_CHANNEL);

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
