/*
 * servoControl.cpp
 * ────────────────
 * Command-driven single-servo controller for automated waste sorting.
 * Implemented using the Arduino framework lifecycle (setup / loop).
 *
 * Receives waste classification packets ("metal", "plastic", "paper_cardboard") wirelessly
 * from the CV ESP32 via ESP-NOW, OR accepts manual commands and raw angles (0-180)
 * directly via USB Serial Monitor.
 */

#include "servoControl.h"
#include <ESP32Servo.h>
#include "config.h"
#include "wasteClassifier.h"
#include "espNowComms.h"

// ── Module-private state ────────────────────────────────────────
static Servo sortingServo;
static int currentServoAngle = SERVO_HOME_ANGLE;

static constexpr uint32_t SWEEP_PAUSE_MS = 1200;
static constexpr uint32_t SERIAL_IDLE_TIMEOUT_MS = 300;
static constexpr uint32_t HEARTBEAT_MS = 10000;
static constexpr unsigned int MAX_ANGLE_DIGITS = 3;

// ── Helper: check if a string is a short non-negative integer ───
static bool isNumeric(const String &str)
{
    if (str.length() == 0 || str.length() > MAX_ANGLE_DIGITS) return false;
    for (unsigned int i = 0; i < str.length(); ++i) {
        if (!isDigit(str.charAt(i))) return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  moveToAngle — Move single servo to physical angle and HOLD
// ─────────────────────────────────────────────────────────────────
void moveToAngle(int targetAngle)
{
    if (targetAngle < SERVO_MIN_ANGLE) targetAngle = SERVO_MIN_ANGLE;
    if (targetAngle > SERVO_MAX_ANGLE) targetAngle = SERVO_MAX_ANGLE;

    Serial.printf("[SERVO] Moving to %d°\n", targetAngle);
    sortingServo.write(targetAngle);
    currentServoAngle = targetAngle;
    Serial.printf("[SERVO] Position reached: %d° (holding position).\n", targetAngle);
}

// ─────────────────────────────────────────────────────────────────
//  sweepTest — Automated test sweep across key positions
// ─────────────────────────────────────────────────────────────────
static void sweepTest()
{
    Serial.println(F("\n[SWEEP] ── Starting Single-Servo Sweep Test ──"));
    const int sweepAngles[] = {SERVO_HOME_ANGLE, ANGLE_METAL, ANGLE_PLASTICS, ANGLE_PAPER,
                               135, SERVO_MAX_ANGLE, SERVO_HOME_ANGLE};
    const size_t count = sizeof(sweepAngles) / sizeof(sweepAngles[0]);

    for (size_t i = 0; i < count; ++i) {
        Serial.printf("[SWEEP] Step %u/%u\n", (unsigned)(i + 1), (unsigned)count);
        moveToAngle(sweepAngles[i]);
        delay(SWEEP_PAUSE_MS);
    }
    Serial.println(F("[SWEEP] Sweep test complete!\n"));
}

// ─────────────────────────────────────────────────────────────────
//  printHelp — Display available Serial commands
// ─────────────────────────────────────────────────────────────────
static void printHelp()
{
    Serial.println(F("\n================ SERVO CONTROLLER — SERIAL COMMANDS ================"));
    Serial.printf("  METAL       -> sort cycle: %d deg, hold %d ms, home\n", ANGLE_METAL, SERVO_HOLD_MS);
    Serial.printf("  PLASTIC(S)  -> sort cycle: %d deg, hold %d ms, home\n", ANGLE_PLASTICS, SERVO_HOLD_MS);
    Serial.printf("  PAPER       -> sort cycle: %d deg, hold %d ms, home\n", ANGLE_PAPER, SERVO_HOLD_MS);
    Serial.printf("  0 - 180     -> move to exact angle and hold\n");
    Serial.printf("  HOME        -> move to rest position (%d deg)\n", SERVO_HOME_ANGLE);
    Serial.println(F("  SWEEP       -> run automatic sweep through all angles"));
    Serial.printf("  SCAN        -> rescan for camera \"%s\" (now on channel %u)\n",
                  CAMERA_AP_SSID, espNowCommsChannel());
    Serial.println(F("  HELP        -> show this menu"));
    Serial.println(F("===================================================================="));
    Serial.print(F("Enter command: "));
}

// ── Helper to read a line from USB Serial non-blockingly ────────
static bool readLineFromStream(Stream &stream, char *buffer, size_t maxLen, size_t &idx)
{
    static uint32_t lastCharTime = 0;

    while (stream.available() > 0)
    {
        char c = (char)stream.read();
        lastCharTime = millis();

        if (c == '\n' || c == '\r') {
            if (idx > 0) {
                buffer[idx] = '\0';
                idx = 0;
                return true;
            }
        } else if (c == '\b' || c == 127) { // Handle backspace
            if (idx > 0) idx--;
        } else if (idx < maxLen - 1) {
            buffer[idx++] = c;
        }
    }

    // Inactivity timeout: if user typed text but terminal sent no newline,
    // process after 300ms idle so "No line ending" mode still works seamlessly.
    if (idx > 0 && (millis() - lastCharTime > SERIAL_IDLE_TIMEOUT_MS)) {
        buffer[idx] = '\0';
        idx = 0;
        return true;
    }

    return false;
}

// ─────────────────────────────────────────────────────────────────
//  sortWasteByTag
// ─────────────────────────────────────────────────────────────────
bool sortWasteByTag(const String &tag)
{
    int targetAngle = tagToAngle(tag);
    if (targetAngle < 0) {
        Serial.printf("[WARN] Unknown waste tag received: \"%s\"\n", tag.c_str());
        return false;
    }

    Serial.printf("[SORT] Detected \"%s\" -> Moving to %d°\n", tag.c_str(), targetAngle);
    sortingServo.write(targetAngle);
    currentServoAngle = targetAngle;

    Serial.printf("[SORT] Holding for %d ms...\n", SERVO_HOLD_MS);
    delay(SERVO_HOLD_MS);

    Serial.printf("[SORT] Returning to home position (%d°)...\n", SERVO_HOME_ANGLE);
    sortingServo.write(SERVO_HOME_ANGLE);
    currentServoAngle = SERVO_HOME_ANGLE;

    Serial.println(F("[SORT] Ready for next item."));
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  servoControlInit
// ─────────────────────────────────────────────────────────────────
void servoControlInit()
{
    // 1. Hardware PWM Timer Allocation for ESP32-S3
    // sortingServo.setPeriodHertz(50);

    // 2. Attach single servo with full 500us - 2500us pulse range
    Serial.printf("[INIT] Attaching sorting servo on GPIO %d...\n", SERVO_PIN);
    Serial.flush();
    sortingServo.attach(SERVO_PIN);

    currentServoAngle = SERVO_HOME_ANGLE;
    sortingServo.write(SERVO_HOME_ANGLE);
    Serial.printf("[INIT] Sorting servo on GPIO %d (Home: %d°)\n", SERVO_PIN, SERVO_HOME_ANGLE);

    // 3. Initialize ESP-NOW wireless communication
    if (!espNowCommsInit()) {
        Serial.println(F("[ERR]  ESP-NOW initialization failed!"));
    }

    // Print command menu
    printHelp();
}

// ─────────────────────────────────────────────────────────────────
//  servoControlLoop — Called from Arduino loop()
// ─────────────────────────────────────────────────────────────────
void servoControlLoop()
{
    WasteSortPacket packet;
    static char bufUSB[64];
    static size_t idxUSB = 0;
    static uint32_t lastHeartbeatMs = 0;

    // Periodic heartbeat log
    if (millis() - lastHeartbeatMs >= HEARTBEAT_MS) {
        lastHeartbeatMs = millis();
        Serial.println(F("[HEARTBEAT] Controller alive. Type METAL / PLASTIC / PAPER / angle / HOME / SWEEP / SCAN / HELP"));
    }

    // 0. Keep ESP-NOW tuned to the camera's channel
    espNowCommsService();

    // 1. Check for incoming packets from ESP-NOW queue
    if (wasteQueue != nullptr && xQueueReceive(wasteQueue, &packet, 0) == pdPASS)
    {
        Serial.printf("[ESP-NOW] Received packet #%u with tag: \"%s\"\n",
                      packet.messageId, packet.tag);
        sortWasteByTag(packet.tag);
        Serial.print(F("\nEnter command: "));
    }

    // 2. Check for manual developer input from USB Serial
    if (readLineFromStream(Serial, bufUSB, sizeof(bufUSB), idxUSB))
    {
        String cmd = String(bufUSB);
        cmd.trim();

        if (cmd.length() > 0)
        {
            Serial.printf("\n>> Command: \"%s\"\n", cmd.c_str());

            if (cmd.equalsIgnoreCase("HELP") || cmd.equals("?")) {
                printHelp();
            }
            else if (cmd.equalsIgnoreCase("HOME") || cmd.equalsIgnoreCase("REST")) {
                moveToAngle(SERVO_HOME_ANGLE);
            }
            else if (cmd.equalsIgnoreCase("SWEEP") || cmd.equalsIgnoreCase("TEST")) {
                sweepTest();
            }
            else if (cmd.equalsIgnoreCase("SCAN")) {
                Serial.printf("[ESP-NOW] Rescanning for \"%s\"...\n", CAMERA_AP_SSID);
                espNowCommsRequestRescan();
            }
            else if (tagToAngle(cmd) >= 0) {
                sortWasteByTag(cmd);
            }
            else if (isNumeric(cmd)) {
                int angle = cmd.toInt();
                if (angle >= SERVO_MIN_ANGLE && angle <= SERVO_MAX_ANGLE) {
                    moveToAngle(angle);
                } else {
                    Serial.printf("[WARN] Angle %d out of range [%d..%d]\n",
                                  angle, SERVO_MIN_ANGLE, SERVO_MAX_ANGLE);
                }
            }
            else {
                Serial.printf("[WARN] Unknown command: \"%s\". Type HELP for commands.\n", cmd.c_str());
            }

            Serial.print(F("\nEnter command: "));
        }
    }

    // Short cooperative yield for ESP32 background tasks (WiFi, watchdog)
    delay(10);
}
