#include "espNowComms.h"
#include <esp_wifi.h>

// Standard Arduino state
WasteSortPacket latestPacket;
volatile bool newPacketAvailable = false;

// Simple FIFO ring buffer (pure C++/Arduino, no FreeRTOS) to ensure no packets are dropped during servo motion
static WasteSortPacket packetBuffer[ESPNOW_BUFFER_SIZE];
static volatile uint8_t ringHead = 0;
static volatile uint8_t ringTail = 0;

// ── Multi-channel discovery and state management ─────────────────
static volatile uint8_t currentChannel = ESPNOW_DEFAULT_CHANNEL;
static volatile bool channelLocked = false;
static volatile uint32_t lastHopTime = 0;
static volatile uint32_t lastPacketTime = 0;
static uint8_t peerMac[6] = {0};
static volatile bool peerKnown = false;
static volatile bool newPeerConnectedNotification = false;

// Pending ACK state (sent from espNowLoop, not inside WiFi ISR context)
static volatile bool pendingAck = false;
static uint8_t pendingAckMac[6] = {0};
static volatile uint32_t pendingAckId = 0;

static const uint8_t broadcastAddress[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ── Helper: Check if string is a discovery/ping tag ─────────────
static bool isDiscoveryTag(const char *tag)
{
    if (!tag || tag[0] == '\0') return false;
    if (strcasecmp(tag, "PING") == 0 ||
        strcasecmp(tag, "BEACON") == 0 ||
        strcasecmp(tag, "DISCOVER") == 0 ||
        strcasecmp(tag, "DISCOVERY") == 0 ||
        strcasecmp(tag, "HELLO") == 0 ||
        strcasecmp(tag, "PROBE") == 0 ||
        strcasecmp(tag, "CONNECT") == 0 ||
        strcasecmp(tag, "WACK") == 0) {
        return true;
    }
    return false;
}

void espNowFlush() {
    ringTail = ringHead;
    newPacketAvailable = false;
}

// ── Helper: Ensure a peer is registered in ESP-NOW table ─────────
static bool ensurePeerRegistered(const uint8_t *mac, uint8_t channel)
{
    if (!mac) return false;
    if (esp_now_is_peer_exist(mac)) {
        return true;
    }
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0; // 0 = send on current radio channel
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    return (esp_now_add_peer(&peer) == ESP_OK);
}

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

    // Lock to this channel immediately upon receiving any valid packet
    if (!channelLocked) {
        newPeerConnectedNotification = true;
    }
    channelLocked = true;
    lastPacketTime = millis();

    if (mac_addr) {
        memcpy(peerMac, mac_addr, 6);
        peerKnown = true;
    }

    WasteSortPacket packet;
    memset(&packet, 0, sizeof(packet));
    bool isDiscovery = false;

    if (data_len == sizeof(CameraWastePacket) ||
        (data_len >= 6 && memcmp(data, "WASTE", 5) == 0 && data_len >= (int)sizeof(CameraWastePacket))) {
        CameraWastePacket camPacket;
        memcpy(&camPacket, data, sizeof(CameraWastePacket));
        camPacket.label[MAX_TAG_LEN - 1] = '\0';
        strncpy(packet.tag, camPacket.label, sizeof(packet.tag) - 1);
        packet.messageId = camPacket.capture_id;
        if (isDiscoveryTag(camPacket.label)) {
            isDiscovery = true;
        }
    } else if (data_len == sizeof(ControllerAckPacket) && memcmp(data, "WACK", 4) == 0) {
        isDiscovery = true;
    } else if (data_len == sizeof(WasteSortPacket)) {
        memcpy(&packet, data, sizeof(WasteSortPacket));
        packet.tag[MAX_TAG_LEN - 1] = '\0'; // sender may fill all 16 bytes
        if (isDiscoveryTag(packet.tag)) {
            isDiscovery = true;
        }
    } else {
        // Fallback for raw string payloads from CV board
        int copyLen = (data_len < (int)(MAX_TAG_LEN - 1)) ? data_len : (int)(MAX_TAG_LEN - 1);
        memcpy(packet.tag, data, copyLen);
        packet.tag[copyLen] = '\0';
        packet.messageId = 0;
        if (isDiscoveryTag(packet.tag)) {
            isDiscovery = true;
        }
    }

    // Schedule ACK response to be sent from espNowLoop
    if (mac_addr) {
        memcpy(pendingAckMac, mac_addr, 6);
    } else {
        memcpy(pendingAckMac, broadcastAddress, 6);
    }
    pendingAckId = packet.messageId;
    pendingAck = true;

    // If packet was only a discovery beacon, do not push to sorting queue
    if (isDiscovery) {
        return;
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
        // if (ringHead == ringTail) {
        //     newPacketAvailable = false;
        // }
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

    channelLocked = false;
    lastHopTime = millis();
    lastPacketTime = 0;
    peerKnown = false;
    newPeerConnectedNotification = false;
    pendingAck = false;
    currentChannel = ESPNOW_DEFAULT_CHANNEL;

    // 2. Set WiFi station mode and channel
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);

    // 3. Print Local MAC Address and status
    Serial.print(F("[INIT] Device ESP-NOW MAC Address: "));
    Serial.println(WiFi.macAddress());
    if (ESPNOW_SCAN_ENABLED) {
        Serial.printf("[INIT] ESP-NOW Channel Auto-Scan: ENABLED (Channels %d-%d, %d ms dwell)\n",
                      ESPNOW_CHANNEL_MIN, ESPNOW_CHANNEL_MAX, ESPNOW_CHANNEL_HOP_MS);
    } else {
        Serial.printf("[INIT] ESP-NOW Channel Fixed: %d\n", currentChannel);
    }

    // 4. Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println(F("[ERR]  ESP-NOW initialization failed!"));
        return false;
    }

    // 5. Register broadcast peer so sending ACKs/announcements always succeeds
    ensurePeerRegistered(broadcastAddress, currentChannel);

    // 6. Register receive callback
    esp_err_t err = esp_now_register_recv_cb(onDataRecv);
    if (err != ESP_OK) {
        Serial.printf("[ERR]  Failed to register ESP-NOW recv callback: %d\n", err);
        return false;
    }

    Serial.println(F("[INIT] ESP-NOW initialized successfully and listening"));
    return true;
}

void espNowLoop()
{
    uint32_t now = millis();

    // 1. If a new peer handshake just locked our channel, log it
    if (newPeerConnectedNotification) {
        newPeerConnectedNotification = false;
        char macStr[18];
        espNowGetPeerMacStr(macStr, sizeof(macStr));
        Serial.printf("\n[ESPNOW] >>> Peer discovered! Channel locked to %d (Peer MAC: %s) <<<\n",
                      currentChannel, macStr);
    }

    // 2. Handle deferred ACK transmission (safely outside ISR context)
    if (pendingAck) {
        pendingAck = false;
        espNowSendAck(pendingAckMac, pendingAckId);
    }

    // 3. Channel hopping loop when searching for peers
    if (!channelLocked && ESPNOW_SCAN_ENABLED) {
        if (now - lastHopTime >= ESPNOW_CHANNEL_HOP_MS) {
            lastHopTime = now;
            currentChannel++;
            if (currentChannel > ESPNOW_CHANNEL_MAX) {
                currentChannel = ESPNOW_CHANNEL_MIN;
            }
            esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
        }
    }

    // 4. Peer inactivity timeout: resume scanning if connection lost
    if (channelLocked && ESPNOW_PEER_TIMEOUT_MS > 0) {
        if (now - lastPacketTime >= ESPNOW_PEER_TIMEOUT_MS) {
            Serial.printf("\n[ESPNOW] Peer inactive for %u ms. Resuming channel scan...\n",
                          (unsigned)ESPNOW_PEER_TIMEOUT_MS);
            channelLocked = false;
            peerKnown = false;
            lastHopTime = now;
        }
    }
}

uint8_t espNowGetCurrentChannel()
{
    return currentChannel;
}

bool espNowIsChannelLocked()
{
    return channelLocked;
}

void espNowStartChannelScan()
{
    channelLocked = false;
    peerKnown = false;
    lastHopTime = millis();
    Serial.printf("[ESPNOW] Manual channel scan started (Channels %d-%d)\n",
                  ESPNOW_CHANNEL_MIN, ESPNOW_CHANNEL_MAX);
}

void espNowSetChannel(uint8_t channel, bool lock)
{
    if (channel < 1) channel = 1;
    if (channel > 14) channel = 14;
    currentChannel = channel;
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    channelLocked = lock;
    if (lock) {
        lastPacketTime = millis();
    }
    Serial.printf("[ESPNOW] Channel manually set to %d (%s)\n",
                  currentChannel, lock ? "LOCKED" : "SCANNING");
}

const uint8_t* espNowGetPeerMac()
{
    return peerKnown ? peerMac : nullptr;
}

void espNowGetPeerMacStr(char *buf, size_t maxLen)
{
    if (!buf || maxLen == 0) return;
    if (peerKnown) {
        snprintf(buf, maxLen, "%02X:%02X:%02X:%02X:%02X:%02X",
                 peerMac[0], peerMac[1], peerMac[2],
                 peerMac[3], peerMac[4], peerMac[5]);
    } else {
        snprintf(buf, maxLen, "NONE (Scanning)");
    }
}

uint32_t espNowGetLastPacketTime()
{
    return lastPacketTime;
}

bool espNowSendAck(const uint8_t *targetMac, uint32_t echoedId)
{
    const uint8_t *dest = targetMac ? targetMac : (peerKnown ? peerMac : broadcastAddress);
    ensurePeerRegistered(dest, currentChannel);

    ControllerAckPacket ack;
    memcpy(ack.magic, "WACK\0", 5);
    ack.channel = currentChannel;
    ack.status = 1; // 1 = Ready
    ack.messageId = echoedId;

    esp_err_t err = esp_now_send(dest, (const uint8_t*)&ack, sizeof(ack));
    return (err == ESP_OK);
}
