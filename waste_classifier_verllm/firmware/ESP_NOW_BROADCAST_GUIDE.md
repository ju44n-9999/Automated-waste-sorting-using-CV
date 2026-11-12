# ESP-NOW Multi-Channel Synchronization & Broadcast Guide

This document provides complete instructions for external devices (such as the ESP32 Computer Vision camera running `WasteClassifierVerLLM` or auxiliary sensors) to broadcast across 2.4 GHz Wi-Fi channels and automatically pair with the **Device Controller (ESP32-S3)**.

---

## 1. Why Multi-Channel Synchronization is Necessary

ESP-NOW transmits raw IEEE 802.11 action frames directly between ESP32 radios without requiring an IP router or network handshake. However, **both transmitter and receiver must operate on the exact same physical 2.4 GHz radio channel (Channels 1–13)**.

In automated waste sorting deployments, channel mismatches frequently happen because:
1. **Wi-Fi AP Router Connection**: When the camera board connects to an office/home Wi-Fi network (e.g., to query Groq LLM or cloud APIs), the router dictates the Wi-Fi channel (which can be any channel between 1 and 13).
2. **Access Point Mode**: When running standalone without a router, the camera may create its own SoftAP on a default channel (e.g., Channel 6).
3. **No Central Router**: Without a shared router to synchronize frequencies, devices booting independently would otherwise need hardcoded channels or manual re-configuration whenever the environment changes.

To solve this, the **Device Controller firmware implements an automatic channel-hopping receiver**, and this guide instructs transmitting devices on how to broadcast across channels to achieve instant, zero-configuration pairing.

---

## 2. Device Controller (Receiver) Behavior

The Device Controller (ESP32-S3) firmware operates with the following automated discovery state machine:

```
┌────────────────────────────────────────────────────────┐
│                   UNLOCKED / SCANNING                  │
│ • Loops Wi-Fi channels 1 through 13                    │
│ • Dwells 200 ms per channel (esp_wifi_set_channel)     │
│ • Listens on Station interface (WIFI_IF_STA)           │
└──────────────────────────┬─────────────────────────────┘
                           │
                           │ Valid ESP-NOW packet received
                           ▼
┌────────────────────────────────────────────────────────┐
│                     CHANNEL LOCKED                     │
│ • Radio frequency immediately locked to peer's channel │
│ • Saves transmitter MAC address                        │
│ • Automatically transmits ACK packet ("WACK") back     │
│ • Enqueues waste sorting tag into ring buffer          │
│ • Dual-servo actuator executes chute sort cycle        │
│ • Inactivity timer (60s): resumes scan if peer lost    │
└────────────────────────────────────────────────────────┘
```

### Supported Inbound Packet Formats:
The controller automatically distinguishes between packet types by size and magic header:
1. **`CameraWastePacket` (34 bytes)** — Standard format from `WasteClassifierVerLLM`:
   - `magic[6]`: `"WASTE"`
   - `capture_id`: sequence number
   - `score`: confidence float
   - `inference_ms`: latency
   - `label[16]`: `"plastic"`, `"metal"`, `"paper_cardboard"`, or discovery tag `"PING"`
2. **`WasteSortPacket` (20 bytes)**:
   - `tag[16]`: `"METAL"`, `"PLASTICS"`, `"PAPER"`, `"PING"`
   - `messageId`: uint32 timestamp or ID
3. **Raw ASCII string**:
   - Any string such as `"PING"`, `"METAL"`, `"PLASTICS"`, `"PAPER"`.

> [!NOTE]
> Discovery tags (`"PING"`, `"BEACON"`, `"DISCOVER"`, `"HELLO"`, `"PROBE"`) immediately lock the controller to the sender's channel and return an ACK, **without triggering servo movement**.

---

## 3. How Other Devices Should Broadcast Across Channels

Transmitting devices can use either of the following two patterns:

### Pattern A: Rapid Channel Sweep on Event (Recommended for Battery / Low Traffic)
When an item is classified, or when sending a periodic heartbeat, cycle through channels 1 through 13 in a tight loop and transmit the packet on each channel.

Because the Device Controller dwells for 200 ms on each channel during scanning, sweeping across all 13 channels (spending ~10 ms per channel) **guarantees that the Device Controller receives the packet in the current dwell window**.

#### C++ Implementation for Transmitting ESP32:

```cpp
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>

// Broadcast address reaches any listening device
static const uint8_t ESPNOW_BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/**
 * @brief Transmit an ESP-NOW packet across all 2.4GHz channels (1 to 13).
 * @param data Pointer to payload struct or string buffer
 * @param len Size of payload in bytes
 */
void broadcastAcrossAllChannels(const uint8_t *data, size_t len) {
    // Preserve current channel if connected to a router
    uint8_t originalChannel = WiFi.channel();
    wifi_second_chan_t secondChan;
    esp_wifi_get_channel(&originalChannel, &secondChan);

    for (uint8_t ch = 1; ch <= 13; ch++) {
        // 1. Hop radio to channel
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        
        // 2. Ensure broadcast peer is configured for this channel
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, ESPNOW_BROADCAST_MAC, 6);
        peer.channel = 0; // 0 = send on current radio interface channel
        peer.ifidx = WiFi.getMode() == WIFI_AP ? WIFI_IF_AP : WIFI_IF_STA;
        peer.encrypt = false;
        
        if (!esp_now_is_peer_exist(ESPNOW_BROADCAST_MAC)) {
            esp_now_add_peer(&peer);
        }

        // 3. Send packet
        esp_now_send(ESPNOW_BROADCAST_MAC, data, len);

        // 4. Short dwell time so packet clears the radio buffer (8-10 ms)
        delay(10);
    }

    // Restore original channel if connected to an AP/Router
    if (originalChannel >= 1 && originalChannel <= 13) {
        esp_wifi_set_channel(originalChannel, WIFI_SECOND_CHAN_NONE);
    }
}
```

---

### Pattern B: Periodic Discovery Beacon on Pinned Channel
If the transmitter cannot hop channels (for example, while actively maintaining a live Wi-Fi station connection to an Internet router or hosting a camera web server):

1. Keep the transmitter on its assigned Wi-Fi channel (`WiFi.channel()`).
2. Broadcast a lightweight discovery ping or waste packet periodically (e.g., every 500 ms) to `FF:FF:FF:FF:FF:FF`:
   - The Device Controller sweeps through channels 1..13 at 200 ms per channel ($13 \times 200\text{ ms} = 2.6\text{ s}$ per full cycle).
   - Within at most **2.6 seconds**, the Device Controller will land on your transmitter's channel, lock to it, and remain locked.
   - Once locked, all subsequent packets are received in **under 4 ms**.

---

## 4. Integration Example: `WasteClassifierVerLLM.ino`

To add multi-channel broadcast directly to the existing camera firmware (`WasteClassifierVerLLM.ino`), add the following function:

```cpp
// ── Multi-Channel Sweep Broadcast for Waste Classification ────────
void sendEspNowMultiChannel(const char *label, float score, uint32_t id, uint32_t inferenceMs) {
    if (!espnow_ready) return;

    WastePacket packet = {};
    memcpy(packet.magic, "WASTE", 5);
    packet.capture_id = id;
    packet.score = score;
    packet.inference_ms = inferenceMs;
    strncpy(packet.label, label, sizeof(packet.label) - 1);

    // If connected to a router, first attempt sending directly on router's channel
    if (WiFi.status() == WL_CONNECTED) {
        esp_now_send(espnow_broadcast, reinterpret_cast<uint8_t *>(&packet), sizeof(packet));
        Serial.printf("[ESP-NOW] Sent %s on connected Wi-Fi channel %d\n", label, WiFi.channel());
    }

    // Sweep across channels 1..13 to ensure controller catches the packet even if scanning
    uint8_t currentCh = WiFi.channel();
    for (uint8_t ch = 1; ch <= 13; ch++) {
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        esp_now_send(espnow_broadcast, reinterpret_cast<uint8_t *>(&packet), sizeof(packet));
        delay(8); // Small transmit margin
    }

    // Restore primary Wi-Fi channel
    if (currentCh >= 1 && currentCh <= 13) {
        esp_wifi_set_channel(currentCh, WIFI_SECOND_CHAN_NONE);
    }
}
```

---

## 5. Listening for Controller Acknowledgment (Optional Two-Way Handshake)

When the Device Controller receives a valid packet on any channel, it transmits a 12-byte `ControllerAckPacket` back to the sender's MAC or to broadcast:

```cpp
typedef struct __attribute__((packed)) {
    char magic[6];          // "WACK\0"
    uint8_t channel;        // Channel controller is locked to (1-13)
    uint8_t status;         // 1 = Ready/Idle, 2 = Sorting
    uint32_t messageId;     // Echoes the received capture_id or messageId
} ControllerAckPacket;
```

To register an acknowledgment callback on the transmitting device:

```cpp
void onControllerAck(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
    if (data_len == sizeof(ControllerAckPacket) && memcmp(data, "WACK", 4) == 0) {
        ControllerAckPacket ack;
        memcpy(&ack, data, sizeof(ControllerAckPacket));
        Serial.printf("[ESP-NOW ACK] Controller locked on Channel %d (Status: %d, Echo ID: %u)\n",
                      ack.channel, ack.status, ack.messageId);
    }
}

// In setup() or initEspNow():
esp_now_register_recv_cb(onControllerAck);
```

---

## 6. Serial Monitor Diagnostics & Commands

### Device Controller (ESP32-S3):
Open the USB Serial Monitor at **`115200` baud**. You can test and inspect the channel connection in real time using the following commands:

| Command | Action | Output Example |
| :--- | :--- | :--- |
| `STATUS` | View active channel, lock state, peer MAC, and servo angle | `│ WiFi Channel: 6 (LOCKED)`<br>`│ Connected Peer: 34:85:18:XX:XX:XX` |
| `SCAN` | Force unlock and resume hopping channels 1–13 | `[ESPNOW] Manual channel scan started (Channels 1-13)` |
| `CH <n>` | Manually lock to a specific channel (e.g., `CH 6`, `CH 11`) | `[ESPNOW] Channel manually set to 6 (LOCKED)` |
| `HELP` | Display all available commands | Shows sorting & channel commands |
| `METAL` | Manually test sort cycle for Metal | Chute opens to 75° |
| `PLASTICS` | Manually test sort cycle for Plastics | Chute opens to 45° |
| `PAPER` | Manually test sort cycle for Paper | Chute opens to 20° |

### Verification Checklist:
- [ ] Power on Device Controller: observe `[INIT] ESP-NOW Channel Auto-Scan: ENABLED (Channels 1-13)`.
- [ ] Power on Camera or CV transmitter: verify broadcast packet is emitted.
- [ ] Observe Device Controller log: `[ESPNOW] >>> Peer discovered! Channel locked to X (Peer MAC: ...) <<<`.
- [ ] Verify servo moves smoothly to the designated chute angle and holds for passage.
