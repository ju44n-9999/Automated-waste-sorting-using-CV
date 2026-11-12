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

/**
 * @brief Acknowledge / discovery response packet sent by Device Controller
 */
typedef struct __attribute__((packed)) {
    char magic[6];          // "WACK\0"
    uint8_t channel;        // Channel controller is locked to
    uint8_t status;         // 1 = Ready/Idle, 2 = Sorting
    uint32_t messageId;     // Echoes received capture_id / messageId
} ControllerAckPacket;

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

/**
 * @brief Periodic communication task called from Arduino loop().
 *        Handles channel hopping when searching for peers and peer timeouts.
 */
void espNowLoop();

/**
 * @brief Gets the active Wi-Fi channel currently tuned by the radio.
 */
uint8_t espNowGetCurrentChannel();

/**
 * @brief Returns true if locked onto an active peer's channel.
 */
bool espNowIsChannelLocked();

/**
 * @brief Starts or forces channel hopping loop to discover other devices.
 */
void espNowStartChannelScan();

/**
 * @brief Manually tunes and locks to a specific Wi-Fi channel.
 * @param channel Wi-Fi channel 1-13
 * @param lock If true, locks to this channel and disables auto-hopping
 */
void espNowSetChannel(uint8_t channel, bool lock = true);

/**
 * @brief Gets the MAC address of the last connected peer (6 bytes).
 * @return Pointer to 6-byte array or nullptr if no peer seen yet
 */
const uint8_t* espNowGetPeerMac();

/**
 * @brief Formats the peer MAC address into a string buffer (e.g. "AA:BB:CC:DD:EE:FF").
 * @param buf Output buffer (at least 18 bytes)
 * @param maxLen Size of buffer
 */
void espNowGetPeerMacStr(char *buf, size_t maxLen);

/**
 * @brief Gets the timestamp (millis) of the last valid packet received.
 */
uint32_t espNowGetLastPacketTime();

/**
 * @brief Sends an ACK packet back to the peer or broadcast.
 * @param targetMac Target MAC address (or nullptr for last seen peer)
 * @param echoedId Message ID to echo back
 * @return true on success, false on error
 */
bool espNowSendAck(const uint8_t *targetMac = nullptr, uint32_t echoedId = 0);

/**
 * @brief Clears pending packets in the queue (e.g., after physical actuation completes).
 */
void espNowFlush();

#endif // ESP_NOW_COMMS_H