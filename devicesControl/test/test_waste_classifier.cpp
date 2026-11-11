#include <cassert>
#include <iostream>
#include <string>
#include "../include/config.h"
#include "../lib/wasteClassifier/wasteClassifier.h"

// Simple lightweight assertion runner
#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (line " << __LINE__ << ")\n"; \
            return 1; \
        } \
    } while (0)

int main()
{
    std::cout << "Running Waste Classifier & Servo Angle Tests...\n";

    // ── Test 1: Standard tags to angles ──
    TEST_ASSERT(tagToAngle("METAL") == ANGLE_METAL, "Tag 'METAL' must return ANGLE_METAL");
    TEST_ASSERT(tagToAngle("PLASTICS") == ANGLE_PLASTICS, "Tag 'PLASTICS' must return ANGLE_PLASTICS");
    TEST_ASSERT(tagToAngle("PLASTIC") == ANGLE_PLASTICS, "Tag 'PLASTIC' must return ANGLE_PLASTICS");
    TEST_ASSERT(tagToAngle("PAPER") == ANGLE_PAPER, "Tag 'PAPER' must return ANGLE_PAPER");
    TEST_ASSERT(tagToAngle("paper_cardboard") == ANGLE_PAPER, "Tag 'paper_cardboard' must return ANGLE_PAPER");
    TEST_ASSERT(tagToAngle("CARDBOARD") == ANGLE_PAPER, "Tag 'CARDBOARD' must return ANGLE_PAPER");

    // ── Test 2: Case insensitivity ──
    TEST_ASSERT(tagToAngle("metal") == ANGLE_METAL, "Tag 'metal' lowercase must return ANGLE_METAL");
    TEST_ASSERT(tagToAngle("Plastics") == ANGLE_PLASTICS, "Tag 'Plastics' mixed-case must return ANGLE_PLASTICS");
    TEST_ASSERT(tagToAngle("paper") == ANGLE_PAPER, "Tag 'paper' lowercase must return ANGLE_PAPER");

    // ── Test 3: Whitespace, \r, \n handling ──
    TEST_ASSERT(tagToAngle("METAL\n") == ANGLE_METAL, "Tag 'METAL\\n' must return ANGLE_METAL");
    TEST_ASSERT(tagToAngle("PLASTICS\r\n") == ANGLE_PLASTICS, "Tag 'PLASTICS\\r\\n' must return ANGLE_PLASTICS");
    TEST_ASSERT(tagToAngle("  PAPER  \n") == ANGLE_PAPER, "Tag with padding must return ANGLE_PAPER");

    // ── Test 4: Unknown / invalid tags ──
    TEST_ASSERT(tagToAngle("GLASS") == -1, "Unknown tag 'GLASS' must return -1");
    TEST_ASSERT(tagToAngle("ORGANIC") == -1, "Unknown tag 'ORGANIC' must return -1");
    TEST_ASSERT(tagToAngle("") == -1, "Empty tag must return -1");
    TEST_ASSERT(tagToAngle(static_cast<const char*>(nullptr)) == -1, "Null tag must return -1");

    // ── Test 5: Facing Servo 2 mirrored angle calculations ──
    // Formula: servo2 = 180 - servo1
    TEST_ASSERT(calculateMirroredAngle(0) == 180, "Servo1=0 -> Servo2=180");
    TEST_ASSERT(calculateMirroredAngle(20) == 160, "Servo1=20 (METAL) -> Servo2=160");
    TEST_ASSERT(calculateMirroredAngle(45) == 135, "Servo1=45 (PLASTICS) -> Servo2=135");
    TEST_ASSERT(calculateMirroredAngle(90) == 90, "Servo1=90 (PAPER) -> Servo2=90");
    TEST_ASSERT(calculateMirroredAngle(180) == 0, "Servo1=180 -> Servo2=0");

    std::cout << "All 20 unit tests PASSED successfully!\n";
    return 0;
}