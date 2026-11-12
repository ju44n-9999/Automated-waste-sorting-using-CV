#ifndef CONFIG_H
#define CONFIG_H

// ── Servo angle limits (Standard 180° Positional Servos) ─────────
#define SERVO_MIN_ANGLE    0
#define SERVO_MAX_ANGLE  180
#define SERVO_HOME_ANGLE   95     // Rest position (gate closed)

// ── Sorting angles per waste tag (keep in sync with tags.txt) ───
#define ANGLE_METAL       75     // METAL
#define ANGLE_PLASTICS    45     // PLASTIC / PLASTICS
#define ANGLE_PAPER       20     // PAPER / PAPER_CARDBOARD / CARDBOARD

// ── Servo movement timing ───────────────────────────────────────
#define SERVO_HOLD_MS   5000     // Hold at target before returning home

// NOTE: All angle logic assumes POSITIONAL 180° servos. "360°" servos are
// continuous-rotation: write() sets speed (90 = stop), so angles won't work.

// ── ESP-NOW Wireless Communication ──────────────────────────────
#define CAMERA_AP_SSID   "WasteCamera"  // Camera hotspot; we scan for it to find its channel
#define ESPNOW_WIFI_CHANNEL        6     // Fallback channel if the hotspot is not found (camera default)
#define ESPNOW_RESCAN_IDLE_MS  60000     // Rescan for the camera after this long without packets
#define WASTE_QUEUE_LENGTH        10     // Max pending waste items buffered in FreeRTOS queue

// ── Servo GPIO pin ───────────────────────────────────────────────
#define SERVO_PIN    4   // Single sorting servo pin
#define SERVO_1_PIN  SERVO_PIN // Backwards-compatible alias

#endif // CONFIG_H