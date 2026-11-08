#ifndef SERVO_CONTROL_H
#define SERVO_CONTROL_H

#include <Arduino.h>
#include <ESP32Servo.h>
#include "config.h"
#include "pinList.h"
#include "wasteClassifier.h"
#include "espNowComms.h"

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