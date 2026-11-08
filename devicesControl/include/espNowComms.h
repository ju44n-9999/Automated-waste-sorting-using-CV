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

// FreeRTOS queue for decoupling high-priority ESP-NOW callback from servo actuation
extern QueueHandle_t wasteQueue;

/**
 * @brief Initialize WiFi in Station mode, configure ESP-NOW, and print local MAC.
 * @return true on success, false otherwise
 */
bool espNowCommsInit();

#endif // ESP_NOW_COMMS_H