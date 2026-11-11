/*
 * servoControl.cpp
 * ────────────────
 * Command-driven dual-servo controller for automated waste sorting via ESP-NOW.
 *
 * Receives waste classification packets ("metal", "plastic", "paper_cardboard") wirelessly
 * from the CV ESP32 via ESP-NOW into a FreeRTOS queue.
 *
 * Synchronizes two facing servos to move in the exact same physical direction:
 *   - Servo 1: target angle
 *   - Servo 2: 180 - target angle (mirrored)
 */

#include "servoControl.h"

// ── Module-private state ────────────────────────────────────────
static Servo servo1;
static Servo servo2;
static TaskHandle_t servoTaskHandle = nullptr;
static int currentServoAngle = SERVO_HOME_ANGLE;

// ── Smooth move helper ──────────────────────────────────────────
static void smoothMove(int fromAngle, int toAngle)
{
    if (fromAngle == toAngle) {
        servo1.write(toAngle);
        servo2.write(calculateMirroredAngle(toAngle));
        return;
    }

    int current = fromAngle;
    int step = (toAngle > fromAngle) ? SERVO_STEP_DEG : -SERVO_STEP_DEG;

    while (current != toAngle)
    {
        current += step;

        if ((step > 0 && current > toAngle) ||
            (step < 0 && current < toAngle)) {
            current = toAngle;
        }

        servo1.write(current);
        servo2.write(calculateMirroredAngle(current));

        vTaskDelay(pdMS_TO_TICKS(SERVO_STEP_MS));
    }
}

// ── Helper to read a line from USB Serial non-blockingly ────────
static bool readLineFromStream(Stream &stream, char *buffer, size_t maxLen, size_t &idx)
{
    while (stream.available() > 0)
    {
        char c = (char)stream.read();
        if (c == '\n' || c == '\r') {
            if (idx > 0) {
                buffer[idx] = '\0';
                idx = 0;
                return true;
            }
        } else if (idx < maxLen - 1) {
            buffer[idx++] = c;
        }
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

    int mirroredTarget = calculateMirroredAngle(targetAngle);
    Serial.printf("[SORT] Detected \"%s\" -> Moving to %d° (Servo1=%d°, Servo2=%d°)\n",
                  tag.c_str(), targetAngle, targetAngle, mirroredTarget);

    // 1. Move to sort bin position
    smoothMove(currentServoAngle, targetAngle);
    currentServoAngle = targetAngle;

    // 2. Hold at sort position
    Serial.printf("[SORT] Holding for %d ms...\n", SERVO_HOLD_MS);
    vTaskDelay(pdMS_TO_TICKS(SERVO_HOLD_MS));

    // 3. Return to home position
    Serial.printf("[SORT] Returning to home position (%d°)...\n", SERVO_HOME_ANGLE);
    smoothMove(currentServoAngle, SERVO_HOME_ANGLE);
    currentServoAngle = SERVO_HOME_ANGLE;

    Serial.println("[SORT] Ready for next item.");
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  servoControlInit
// ─────────────────────────────────────────────────────────────────
void servoControlInit()
{
    // 1. Attach and park servos
    servo1.attach(SERVO_1_PIN);
    servo2.attach(SERVO_2_PIN);

    currentServoAngle = SERVO_HOME_ANGLE;
    servo1.write(SERVO_HOME_ANGLE);
    servo2.write(calculateMirroredAngle(SERVO_HOME_ANGLE));

    Serial.printf("[INIT] Servo 1 on GPIO %d (Home: %d°)\n", SERVO_1_PIN, SERVO_HOME_ANGLE);
    Serial.printf("[INIT] Servo 2 on GPIO %d (Home: %d°)\n", SERVO_2_PIN, calculateMirroredAngle(SERVO_HOME_ANGLE));

    // 2. Initialize ESP-NOW wireless communication
    if (!espNowCommsInit()) {
        Serial.println("[ERR]  ESP-NOW initialization failed!");
    }

    // 3. Spawn FreeRTOS listener task
    BaseType_t ok = xTaskCreatePinnedToCore(
        servoListenTask,
        "ServoListen",
        SERVO_TASK_STACK,
        nullptr,
        SERVO_TASK_PRIO,
        &servoTaskHandle,
        SERVO_TASK_CORE
    );

    if (ok != pdPASS) {
        Serial.println("[ERR]  Failed to create ServoListen FreeRTOS task!");
    } else {
        Serial.println("[INIT] ServoListen FreeRTOS task running on core 1");
    }
}

// ─────────────────────────────────────────────────────────────────
//  servoListenTask
// ─────────────────────────────────────────────────────────────────
void servoListenTask(void * /* pvParameters */)
{
    WasteSortPacket packet;
    static char bufUSB[64];
    static size_t idxUSB = 0;

    for (;;)
    {
        // 1. Check for incoming packets from ESP-NOW FreeRTOS queue
        if (wasteQueue != nullptr && xQueueReceive(wasteQueue, &packet, pdMS_TO_TICKS(20)) == pdPASS)
        {
            Serial.printf("[ESP-NOW] Received packet #%u with tag: \"%s\"\n",
                          packet.messageId, packet.tag);
            sortWasteByTag(packet.tag);
        }

        // 2. Check for manual developer input from USB Serial
        if (readLineFromStream(Serial, bufUSB, sizeof(bufUSB), idxUSB))
        {
            String tag = String(bufUSB);
            tag.trim();
            if (tag.equalsIgnoreCase("SCAN")) {
                espNowRequestRescan();
            } else if (tag.length() > 0) {
                Serial.printf("[USB-MANUAL] Received tag: \"%s\"\n", tag.c_str());
                sortWasteByTag(tag);
            }
        }
    }
}