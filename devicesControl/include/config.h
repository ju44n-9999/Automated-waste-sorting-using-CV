#ifndef CONFIG_H
#define CONFIG_H

// ── Servo angle limits (Standard 180° Positional Servos) ─────────
#define SERVO_MIN_ANGLE    0
#define SERVO_MAX_ANGLE  180
#define SERVO_HOME_ANGLE   0     // Rest position (gate closed)

// ── Sorting angles per waste tag ────────────────────────────────
#define ANGLE_METAL       20     // 20 degrees for METAL
#define ANGLE_PLASTICS    45     // 45 degrees for PLASTICS
#define ANGLE_PAPER       90     // 90 degrees for PAPER

// ── Servo movement timing ───────────────────────────────────────
#define SERVO_STEP_DEG     1     // Degrees per step (smooth movement)
#define SERVO_STEP_MS     10     // Milliseconds between steps
#define SERVO_HOLD_MS   1500     // Hold at target before returning home

// NOTE: All angle logic assumes POSITIONAL 180° servos. "360°" servos are
// continuous-rotation: write() sets speed (90 = stop), so angles won't work.

// ── ESP-NOW Wireless Communication ──────────────────────────────
#define ESPNOW_WIFI_CHANNEL        1     // WiFi channel for ESP-NOW (must match CV sender)
#define WASTE_QUEUE_LENGTH        10     // Max pending waste items buffered in FreeRTOS queue

// ── FreeRTOS task settings ──────────────────────────────────────
#define SERVO_TASK_STACK        4096     // Stack size (bytes)
#define SERVO_TASK_PRIO            2     // Task priority
#define SERVO_TASK_CORE            1     // Core affinity (1 = APP core)

// ── Servo GPIO pin ───────────────────────────────────────────────
#define SERVO_PIN    5   // Single sorting servo pin
#define SERVO_1_PIN  SERVO_PIN // Backwards-compatible alias

#endif // CONFIG_H