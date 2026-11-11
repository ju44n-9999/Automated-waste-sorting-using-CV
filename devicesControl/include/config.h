#ifndef CONFIG_H
#define CONFIG_H

// ── Servo angle limits (Standard 180° Positional Servos) ─────────
#define SERVO_MIN_ANGLE    0
#define SERVO_MAX_ANGLE  180
#define SERVO_HOME_ANGLE   95     // Rest position (gate closed)

// ── Sorting angles per waste tag ────────────────────────────────
#define ANGLE_METAL       75     // 20 degrees for METAL
#define ANGLE_PLASTICS    45     // 45 degrees for PLASTICS
#define ANGLE_PAPER       20     // 90 degrees for PAPER

// ── Servo movement timing ───────────────────────────────────────
#define SERVO_HOLD_MS   5000     // Hold at target before returning home

// NOTE: All angle logic assumes POSITIONAL 180° servos. "360°" servos are
// continuous-rotation: write() sets speed (90 = stop), so angles won't work.

// ── ESP-NOW Wireless Communication ──────────────────────────────
#define ESPNOW_WIFI_CHANNEL        1     // WiFi channel for ESP-NOW (must match CV sender)
#define WASTE_QUEUE_LENGTH        10     // Max pending waste items buffered in FreeRTOS queue

// ── Servo GPIO pin ───────────────────────────────────────────────
#define SERVO_PIN    4   // Single sorting servo pin
#define SERVO_1_PIN  SERVO_PIN // Backwards-compatible alias

#endif // CONFIG_H