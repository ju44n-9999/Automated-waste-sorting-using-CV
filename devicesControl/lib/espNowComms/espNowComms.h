#ifndef ESP_NOW_COMMS_H
#define ESP_NOW_COMMS_H

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "config.h"

#define MAX_TAG_LEN 16

/**
 * @brief Queue item handed to the servo task (also the legacy 20-byte wire format).
 */
typedef struct __attribute__((packed)) {
    char tag[MAX_TAG_LEN]; // "metal", "plastic", "paper_cardboard", ...
    uint32_t messageId;    // capture_id from the camera, 0 for raw strings
} WasteSortPacket;

/**
 * @brief Packet broadcast by the camera firmware (WastePacket in WasteClassifierVerLLM.ino).
 * Both boards are ESP32-S3, so the little-endian packed layout matches byte for byte.
 */
typedef struct __attribute__((packed)) {
    char magic[6];          // "WASTE\0"
    uint32_t capture_id;
    float score;            // -1 when the label came from the LLM (no confidence)
    uint32_t inference_ms;
    char label[16];         // "plastic", "metal", "paper_cardboard"
} CameraPacket;

static_assert(sizeof(CameraPacket) == 34, "CameraPacket must match the camera's WastePacket");
static_assert(sizeof(WasteSortPacket) == 20, "WasteSortPacket wire size changed");

// FreeRTOS queue for decoupling high-priority ESP-NOW callback from servo actuation
extern QueueHandle_t wasteQueue;

/**
 * @brief Start Wi-Fi (station, not connected), find the camera hotspot, tune to its
 *        channel, initialize ESP-NOW and start the background channel-tracking task.
 * @return true on success, false otherwise
 */
bool espNowCommsInit();

/**
 * @brief Forget the locked camera and scan again (serial command "SCAN").
 */
void espNowRequestRescan();

#endif // ESP_NOW_COMMS_H
