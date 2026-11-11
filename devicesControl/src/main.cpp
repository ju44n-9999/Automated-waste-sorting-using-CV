/*
 * main.cpp — Waste Sorting Servo Controller
 * Arduino framework entry point (setup and loop).
 */

#include <Arduino.h>
#include "servoControl.h"

void setup()
{
    Serial.begin(115200);
    delay(500); // Allow serial connection to stabilize

    Serial.println(F("\n[INIT] Waste Sorting Servo Controller starting..."));
    servoControlInit();
}

void loop()
{
    servoControlLoop();
}
