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
    char tag[MAX_TAG_LEN]; // "METAL", "PLASTIC", "PAPER"
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

// FreeRTOS queue for decoupling high-priority ESP-NOW callback from servo actuation
extern QueueHandle_t wasteQueue;

/**
 * @brief Initialize WiFi in Station mode, tune to the camera hotspot's channel,
 *        configure ESP-NOW, and print local MAC.
 * @return true on success, false otherwise
 */
bool espNowCommsInit();

/**
 * @brief Call from the main loop. Finishes pending channel scans and rescans
 *        for the camera after ESPNOW_RESCAN_IDLE_MS without packets.
 */
void espNowCommsService();

/** @brief Ask espNowCommsService() to rescan for the camera on its next call. */
void espNowCommsRequestRescan();

/** @brief Wi-Fi channel ESP-NOW is currently listening on. */
uint8_t espNowCommsChannel();

#endif // ESP_NOW_COMMS_H