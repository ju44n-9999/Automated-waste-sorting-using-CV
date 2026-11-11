#ifndef ESP_NOW_COMMS_H
#define ESP_NOW_COMMS_H

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "config.h"

#define MAX_TAG_LEN 16

/**
 * @brief Packet structure sent from CV ESP32 to Device Controller ESP32
 */
typedef struct __attribute__((packed)) {
    char tag[MAX_TAG_LEN]; // "METAL", "PLASTICS", "PAPER"
    uint32_t messageId;    // Sequential ID or timestamp
} WasteSortPacket;

/**
 * @brief Packet structure sent by camera firmware (WasteClassifierVerLLM)
 */
typedef struct __attribute__((packed)) {
    char magic[6];          // "WASTE"
    uint32_t capture_id;
    float score;
    uint32_t inference_ms;
    char label[MAX_TAG_LEN];
} CameraWastePacket;

// Standard Arduino-style received packet buffer and flag
extern WasteSortPacket latestPacket;
extern volatile bool newPacketAvailable;

/**
 * @brief Checks if a new ESP-NOW packet is available to process.
 * @return true if a packet is available, false otherwise
 */
bool espNowAvailable();

/**
 * @brief Reads the next available ESP-NOW packet.
 * @param packet Reference to WasteSortPacket to receive data
 * @return true if a packet was retrieved, false otherwise
 */
bool espNowRead(WasteSortPacket &packet);

/**
 * @brief Reads the next available ESP-NOW packet (pointer overload).
 */
inline bool espNowRead(WasteSortPacket *packet) {
    return packet ? espNowRead(*packet) : false;
}

/**
 * @brief Initialize WiFi in Station mode, configure ESP-NOW, and print local MAC.
 * @return true on success, false otherwise
 */
bool espNowCommsInit();

#endif // ESP_NOW_COMMS_H