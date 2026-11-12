#ifndef CONFIG_H
#define CONFIG_H

// ── Servo angle limits (Standard 180° Positional Servos) ─────────
#define SERVO_MIN_ANGLE    0
#define SERVO_MAX_ANGLE  180
#define SERVO_HOME_ANGLE   100     // Rest position (gate closed)

// ── Sorting angles per waste tag ────────────────────────────────
#define ANGLE_METAL       80     // 20 degrees for METAL
#define ANGLE_PLASTICS    55     // 45 degrees for PLASTICS
#define ANGLE_PAPER       20     // 90 degrees for PAPER

// ── Servo movement timing ───────────────────────────────────────
#define SERVO_HOLD_MS   2000     // Hold at target before returning home
#define SERVO_STEP_DEG     1     // Degrees per step for smooth acceleration
#define SERVO_STEP_MS     10     // Delay between steps in ms

// NOTE: All angle logic assumes POSITIONAL 180° servos. "360°" servos are
// continuous-rotation: write() sets speed (90 = stop), so angles won't work.

// ── ESP-NOW Wireless Communication ──────────────────────────────
#define ESPNOW_WIFI_CHANNEL       6      // Default/fallback WiFi channel
#define ESPNOW_DEFAULT_CHANNEL    ESPNOW_WIFI_CHANNEL
#define ESPNOW_SCAN_ENABLED       true   // Enable automatic channel hopping when searching for peers
#define ESPNOW_CHANNEL_MIN        1      // Lowest 2.4GHz WiFi channel to scan
#define ESPNOW_CHANNEL_MAX        13     // Highest 2.4GHz WiFi channel to scan
#define ESPNOW_CHANNEL_HOP_MS     200    // Dwell time per channel during scan (ms)
#define ESPNOW_PEER_TIMEOUT_MS    0  // Inactivity timeout (ms) before resuming channel scan (0 = stay locked)
#define ESPNOW_BUFFER_SIZE        10     // Max pending waste items buffered
#define WASTE_QUEUE_LENGTH        ESPNOW_BUFFER_SIZE // Backwards-compatible alias

// ── Servo GPIO pin ───────────────────────────────────────────────
#define SERVO_PIN    4   // Single sorting servo pin
#define SERVO_1_PIN  SERVO_PIN // Backwards-compatible alias

#endif // CONFIG_H