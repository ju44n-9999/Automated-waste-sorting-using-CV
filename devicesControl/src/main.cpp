/*
 * main.cpp — Waste Sorting Servo Controller
 * Entry point — initialises Serial and delegates to servoControl.
 */

#include <Arduino.h>
#include "servoControl.h"

void setup()
{
    Serial.begin(115200);
    while (!Serial) { delay(10); }

    Serial.println("[INIT] Waste Sorting Servo Controller");

    servoControlInit();
}

void loop()
{
    // All work runs in RTOS tasks
    vTaskDelay(pdMS_TO_TICKS(1000));
}