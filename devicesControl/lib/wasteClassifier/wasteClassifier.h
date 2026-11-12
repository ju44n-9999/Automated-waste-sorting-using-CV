#ifndef WASTE_CLASSIFIER_H
#define WASTE_CLASSIFIER_H

#include "config.h"

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <string>
#endif

/**
 * @brief Maps a waste category tag to target servo angle.
 * 
 * Rules (angles from config.h, see tags.txt):
 *   - "METAL"    -> ANGLE_METAL    (75)
 *   - "PLASTICS" -> ANGLE_PLASTICS (45) (also accepts "PLASTIC")
 *   - "PAPER"    -> ANGLE_PAPER    (20) (also "PAPER_CARDBOARD", "CARDBOARD")
 * 
 * @param tag String or C-string tag (case-insensitive, trims whitespace)
 * @return int Target angle, or -1 if unknown
 */
int tagToAngle(const char* tag);

#ifdef ARDUINO
inline int tagToAngle(const String& tag) {
    return tagToAngle(tag.c_str());
}
#else
inline int tagToAngle(const std::string& tag) {
    return tagToAngle(tag.c_str());
}
#endif

/**
 * @brief Calculate mirrored angle for Servo 2 facing Servo 1.
 * 
 * @param servo1Angle Angle commanded to Servo 1
 * @return int Mirrored angle for Servo 2 (180 - servo1Angle)
 */
inline int calculateMirroredAngle(int servo1Angle) {
    return SERVO_MAX_ANGLE - servo1Angle;
}

#endif // WASTE_CLASSIFIER_H