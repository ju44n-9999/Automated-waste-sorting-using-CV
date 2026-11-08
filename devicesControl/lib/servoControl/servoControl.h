#ifndef SERVO_CONTROL_H
#define SERVO_CONTROL_H

#include <Arduino.h>
#include <ESP32Servo.h>
#include "config.h"
#include "wasteClassifier.h"
#include "espNowComms.h"

// ── Servo GPIO pins ──────────────────────────────────────────────
#define SERVO_1_PIN  4   // First servo  (e.g. left side)
#define SERVO_2_PIN  5   // Second servo (right side, facing first)

// ── Optional Wired UART Fallback (if ESP-NOW is not used) ────────
#define OPTIONAL_UART_RX  16
#define OPTIONAL_UART_TX  17

/**
 * @brief Initialize both servos, ESP-NOW wireless communication,
 *        and spawn the FreeRTOS processing task.
 */
void servoControlInit();

/**
 * @brief FreeRTOS task: consumes packets from ESP-NOW queue and USB serial
 *        to actuate the waste sorting servos.
 */
void servoListenTask(void *pvParameters);

/**
 * @brief Actuate servos to the assigned angle for a detected tag,
 *        hold for duration, and return home.
 * 
 * @param tag Detected tag ("METAL", "PLASTICS", "PAPER")
 * @return true if valid tag was recognized and executed
 */
bool sortWasteByTag(const String &tag);

#endif // SERVO_CONTROL_H