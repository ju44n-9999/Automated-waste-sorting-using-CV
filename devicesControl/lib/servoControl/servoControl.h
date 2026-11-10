#ifndef SERVO_CONTROL_H
#define SERVO_CONTROL_H

#include <Arduino.h>

/**
 * @brief Initialize single sorting servo, PWM timer, and communications.
 */
void servoControlInit();

/**
 * @brief Main periodic service loop to be invoked in Arduino loop().
 *        Handles USB Serial command input and pending ESP-NOW packets.
 */
void servoControlLoop();

/**
 * @brief Move servo to the angle for a waste tag, hold SERVO_HOLD_MS, return home.
 * @param tag "METAL", "PLASTICS", "PAPER", etc. (case-insensitive)
 * @return true if the tag was recognized and the cycle ran
 */
bool sortWasteByTag(const String &tag);

/**
 * @brief Smoothly move single servo to target angle and hold.
 * @param targetAngle 0..180, clamped
 */
void moveToAngle(int targetAngle);

#endif // SERVO_CONTROL_H
