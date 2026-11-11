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
    TEST_ASSERT(tagToAngle("METAL") == 20, "Tag 'METAL' must return 20 degrees");
    TEST_ASSERT(tagToAngle("PLASTICS") == 45, "Tag 'PLASTICS' must return 45 degrees");
    TEST_ASSERT(tagToAngle("PLASTIC") == 45, "Tag 'PLASTIC' must return 45 degrees");
    TEST_ASSERT(tagToAngle("PAPER") == 90, "Tag 'PAPER' must return 90 degrees");
    TEST_ASSERT(tagToAngle("paper_cardboard") == 90, "Tag 'paper_cardboard' must return 90 degrees");
    TEST_ASSERT(tagToAngle("CARDBOARD") == 90, "Tag 'CARDBOARD' must return 90 degrees");

    // ── Test 2: Case insensitivity ──
    TEST_ASSERT(tagToAngle("metal") == 20, "Tag 'metal' lowercase must return 20");
    TEST_ASSERT(tagToAngle("Plastics") == 45, "Tag 'Plastics' mixed-case must return 45");
    TEST_ASSERT(tagToAngle("paper") == 90, "Tag 'paper' lowercase must return 90");

    // ── Test 3: Whitespace, \r, \n handling ──
    TEST_ASSERT(tagToAngle("METAL\n") == 20, "Tag 'METAL\\n' must return 20");
    TEST_ASSERT(tagToAngle("PLASTICS\r\n") == 45, "Tag 'PLASTICS\\r\\n' must return 45");
    TEST_ASSERT(tagToAngle("  PAPER  \n") == 90, "Tag with padding must return 90");

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