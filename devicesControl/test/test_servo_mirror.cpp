/**
 * @file test_servo_mirror.cpp
 * @brief TDD tests: verify facing-servo mirror angle mapping ensures
 *        two MG996R servos mounted face-to-face rotate in the SAME
 *        physical direction.
 *
 * Physical setup:
 *   Servo 1 (left)  ←——facing——→  Servo 2 (right)
 *   Both horn shafts point inward toward each other.
 *
 * Mirror formula: servo2_angle = 180 - servo1_angle
 *
 * WHY mirroring is necessary:
 *   When two servos face each other, identical PWM angles cause their
 *   arms to swing in OPPOSITE physical directions.  Complementing
 *   Servo 2's angle (180° - θ) makes both arms move the SAME way.
 *
 * Run:
 *   g++ -std=c++17 -Iinclude -Ilib/wasteClassifier \
 *       lib/wasteClassifier/wasteClassifier.cpp \
 *       test/test_servo_mirror.cpp -o test_servo_mirror.exe
 *   ./test_servo_mirror.exe
 */

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include "../include/config.h"
#include "../lib/wasteClassifier/wasteClassifier.h"

// ── Lightweight test macros ────────────────────────────────────────
static int g_testCount = 0;
static int g_failCount = 0;

#define TEST_ASSERT(cond, msg)                                          \
    do {                                                                \
        ++g_testCount;                                                  \
        if (!(cond)) {                                                  \
            std::cerr << "FAILED [" << g_testCount << "]: " << msg      \
                      << " (line " << __LINE__ << ")\n";                \
            ++g_failCount;                                              \
        } else {                                                        \
            std::cout << "  OK  [" << g_testCount << "]: " << msg       \
                      << "\n";                                          \
        }                                                               \
    } while (0)

// ═══════════════════════════════════════════════════════════════════
//  Section 1: Mirror formula correctness
// ═══════════════════════════════════════════════════════════════════

void testMirrorFormulaBasic()
{
    std::cout << "\n── Section 1: Mirror formula basic correctness ──\n";

    // Identity: servo2 = 180 - servo1
    TEST_ASSERT(calculateMirroredAngle(0)   == 180, "servo1=0   → servo2=180 (home)");
    TEST_ASSERT(calculateMirroredAngle(90)  ==  90, "servo1=90  → servo2=90  (center)");
    TEST_ASSERT(calculateMirroredAngle(180) ==   0, "servo1=180 → servo2=0   (full sweep)");
}

void testMirrorFormulaWasteTags()
{
    std::cout << "\n── Section 2: Mirror formula per waste tag ──\n";

    // METAL: θ=20 → servo2=160
    int metalAngle = tagToAngle("METAL");
    int metalMirror = calculateMirroredAngle(metalAngle);
    TEST_ASSERT(metalAngle  ==  20, "METAL → servo1 = 20°");
    TEST_ASSERT(metalMirror == 160, "METAL → servo2 = 160° (180-20)");

    // PLASTICS: θ=45 → servo2=135
    int plasticsAngle = tagToAngle("PLASTICS");
    int plasticsMirror = calculateMirroredAngle(plasticsAngle);
    TEST_ASSERT(plasticsAngle  ==  45, "PLASTICS → servo1 = 45°");
    TEST_ASSERT(plasticsMirror == 135, "PLASTICS → servo2 = 135° (180-45)");

    // PAPER: θ=90 → servo2=90  (symmetric)
    int paperAngle = tagToAngle("PAPER");
    int paperMirror = calculateMirroredAngle(paperAngle);
    TEST_ASSERT(paperAngle  == 90, "PAPER → servo1 = 90°");
    TEST_ASSERT(paperMirror == 90, "PAPER → servo2 = 90° (symmetric center)");
}

// ═══════════════════════════════════════════════════════════════════
//  Section 2: Involution property  mirror(mirror(x)) == x
// ═══════════════════════════════════════════════════════════════════

void testMirrorInvolution()
{
    std::cout << "\n── Section 3: Involution: mirror(mirror(θ)) == θ ──\n";

    for (int angle = 0; angle <= 180; angle += 15) {
        int result = calculateMirroredAngle(calculateMirroredAngle(angle));
        std::string msg = "mirror(mirror(" + std::to_string(angle) + ")) == " + std::to_string(angle);
        TEST_ASSERT(result == angle, msg);
    }
}

// ═══════════════════════════════════════════════════════════════════
//  Section 3: Physical direction consistency
//  When servo1 increases by Δ, servo2 must DECREASE by Δ.
//  This is what makes two facing servos rotate in the same
//  physical direction.
// ═══════════════════════════════════════════════════════════════════

void testPhysicalDirectionConsistency()
{
    std::cout << "\n── Section 4: Physical direction consistency ──\n";

    // For ANY pair of angles a < b:
    //   servo1 goes from a → b         (increases by b-a)
    //   servo2 goes from (180-a) → (180-b) (decreases by b-a)
    // The opposite PWM direction = same physical direction (facing mount).

    struct TestCase {
        int fromAngle;
        int toAngle;
        std::string label;
    };

    std::vector<TestCase> cases = {
        {  0,  20, "Home→METAL"},
        {  0,  45, "Home→PLASTICS"},
        {  0,  90, "Home→PAPER"},
        { 20,  45, "METAL→PLASTICS"},
        { 45,  90, "PLASTICS→PAPER"},
        { 20,  90, "METAL→PAPER"},
    };

    for (const auto& tc : cases) {
        int s1_from = tc.fromAngle;
        int s1_to   = tc.toAngle;
        int s2_from = calculateMirroredAngle(s1_from);
        int s2_to   = calculateMirroredAngle(s1_to);

        int s1_delta = s1_to - s1_from;   // positive = CW in servo1 frame
        int s2_delta = s2_to - s2_from;   // must be negative (opposite PWM direction)

        // Key invariant: s1_delta + s2_delta == 0
        // This proves they move the SAME physical direction when facing each other.
        std::string msg = tc.label + ": Δservo1(" + std::to_string(s1_delta) +
                          ") + Δservo2(" + std::to_string(s2_delta) + ") == 0";
        TEST_ASSERT(s1_delta + s2_delta == 0, msg);

        // Additional: servo2 delta must be the NEGATIVE of servo1 delta
        std::string msg2 = tc.label + ": servo2 delta is negative of servo1 delta";
        TEST_ASSERT(s2_delta == -s1_delta, msg2);
    }
}

// ═══════════════════════════════════════════════════════════════════
//  Section 4: Sum property — servo1 + servo2 always equals 180°
// ═══════════════════════════════════════════════════════════════════

void testSumProperty()
{
    std::cout << "\n── Section 5: Sum property: servo1 + servo2 == 180 ──\n";

    // This is the core mathematical invariant.
    // For every valid angle, both servo positions must sum to 180°.

    // Exhaustive check for full range
    bool allValid = true;
    int failedAngle = -1;
    for (int angle = 0; angle <= 180; ++angle) {
        int sum = angle + calculateMirroredAngle(angle);
        if (sum != 180) {
            allValid = false;
            failedAngle = angle;
            break;
        }
    }
    TEST_ASSERT(allValid, "servo1 + servo2 == 180 for ALL angles [0..180]");
    if (!allValid) {
        std::cerr << "  First failure at angle=" << failedAngle << "\n";
    }

    // Spot-check the waste-tag angles specifically
    const int wasteAngles[] = {ANGLE_METAL, ANGLE_PLASTICS, ANGLE_PAPER, SERVO_HOME_ANGLE};
    const char* labels[] = {"METAL(20)", "PLASTICS(45)", "PAPER(90)", "HOME(0)"};
    for (int i = 0; i < 4; ++i) {
        int sum = wasteAngles[i] + calculateMirroredAngle(wasteAngles[i]);
        std::string msg = std::string(labels[i]) + ": " +
                          std::to_string(wasteAngles[i]) + " + " +
                          std::to_string(calculateMirroredAngle(wasteAngles[i])) + " == 180";
        TEST_ASSERT(sum == 180, msg);
    }
}

// ═══════════════════════════════════════════════════════════════════
//  Section 5: Home position symmetry
// ═══════════════════════════════════════════════════════════════════

void testHomePosition()
{
    std::cout << "\n── Section 6: Home position correctness ──\n";

    int homeServo1 = SERVO_HOME_ANGLE;
    int homeServo2 = calculateMirroredAngle(SERVO_HOME_ANGLE);

    TEST_ASSERT(homeServo1 == 0,   "Home servo1 = 0° (gate closed)");
    TEST_ASSERT(homeServo2 == 180, "Home servo2 = 180° (gate closed, mirrored)");

    // Both servos at home means both arms are at mechanical stop
    // in opposite PWM but same physical orientation
    TEST_ASSERT(homeServo1 + homeServo2 == 180, "Home positions sum to 180°");
}

// ═══════════════════════════════════════════════════════════════════
//  Section 6: Smooth movement step simulation
//  Simulate what smoothMove() does: stepping 1° at a time.
//  At every intermediate step, the mirror property must hold.
// ═══════════════════════════════════════════════════════════════════

void testSmoothMoveStepConsistency()
{
    std::cout << "\n── Section 7: Smooth move step-by-step mirror consistency ──\n";

    // Simulate Home(0) → METAL(20) move with SERVO_STEP_DEG=1
    int from = SERVO_HOME_ANGLE;
    int to   = ANGLE_METAL;
    int step = (to > from) ? SERVO_STEP_DEG : -SERVO_STEP_DEG;
    int current = from;

    bool allConsistent = true;

    while (current != to) {
        current += step;
        if ((step > 0 && current > to) || (step < 0 && current < to)) {
            current = to;
        }

        int s1 = current;
        int s2 = calculateMirroredAngle(current);

        if (s1 + s2 != 180) {
            allConsistent = false;
            break;
        }
    }

    TEST_ASSERT(allConsistent,
        "Every step from Home→METAL maintains servo1+servo2==180");

    // Simulate return: METAL(20) → Home(0)
    from = ANGLE_METAL;
    to   = SERVO_HOME_ANGLE;
    step = (to > from) ? SERVO_STEP_DEG : -SERVO_STEP_DEG;
    current = from;
    allConsistent = true;

    while (current != to) {
        current += step;
        if ((step > 0 && current > to) || (step < 0 && current < to)) {
            current = to;
        }

        int s1 = current;
        int s2 = calculateMirroredAngle(current);

        if (s1 + s2 != 180) {
            allConsistent = false;
            break;
        }
    }

    TEST_ASSERT(allConsistent,
        "Every step from METAL→Home maintains servo1+servo2==180");
}

// ═══════════════════════════════════════════════════════════════════
//  Section 7: Boundary & edge-case safety
// ═══════════════════════════════════════════════════════════════════

void testBoundaryAngles()
{
    std::cout << "\n── Section 8: Boundary angles within MG996R range ──\n";

    // All waste-tag angles must be within [0, 180]
    const int angles[] = {ANGLE_METAL, ANGLE_PLASTICS, ANGLE_PAPER, SERVO_HOME_ANGLE};
    const char* names[] = {"METAL", "PLASTICS", "PAPER", "HOME"};

    for (int i = 0; i < 4; ++i) {
        int a = angles[i];
        int m = calculateMirroredAngle(a);

        std::string label(names[i]);
        TEST_ASSERT(a >= SERVO_MIN_ANGLE && a <= SERVO_MAX_ANGLE,
            label + " servo1 angle " + std::to_string(a) + " within [0,180]");
        TEST_ASSERT(m >= SERVO_MIN_ANGLE && m <= SERVO_MAX_ANGLE,
            label + " servo2 angle " + std::to_string(m) + " within [0,180]");
    }
}

// ═══════════════════════════════════════════════════════════════════
//  Section 8: Ordering — distinct tags map to distinct angles
// ═══════════════════════════════════════════════════════════════════

void testDistinctAnglesPerTag()
{
    std::cout << "\n── Section 9: Distinct angles per waste tag ──\n";

    int metal    = tagToAngle("METAL");
    int plastics = tagToAngle("PLASTICS");
    int paper    = tagToAngle("PAPER");

    TEST_ASSERT(metal != plastics, "METAL ≠ PLASTICS angle");
    TEST_ASSERT(plastics != paper,  "PLASTICS ≠ PAPER angle");
    TEST_ASSERT(metal != paper,     "METAL ≠ PAPER angle");

    // Angles should be ordered: METAL < PLASTICS < PAPER
    TEST_ASSERT(metal < plastics, "METAL < PLASTICS (20° < 45°)");
    TEST_ASSERT(plastics < paper, "PLASTICS < PAPER (45° < 90°)");
}

// ═══════════════════════════════════════════════════════════════════
//  Main
// ═══════════════════════════════════════════════════════════════════

int main()
{
    std::cout << "╔═══════════════════════════════════════════════════╗\n";
    std::cout << "║  Servo Mirror Angle Tests — Facing MG996R Setup  ║\n";
    std::cout << "╚═══════════════════════════════════════════════════╝\n";

    testMirrorFormulaBasic();
    testMirrorFormulaWasteTags();
    testMirrorInvolution();
    testPhysicalDirectionConsistency();
    testSumProperty();
    testHomePosition();
    testSmoothMoveStepConsistency();
    testBoundaryAngles();
    testDistinctAnglesPerTag();

    std::cout << "\n══════════════════════════════════════════════════\n";
    if (g_failCount == 0) {
        std::cout << "Result: ALL " << g_testCount << " tests PASSED ✓\n";
    } else {
        std::cout << "Result: " << g_failCount << " / " << g_testCount << " tests FAILED ✗\n";
    }
    std::cout << "══════════════════════════════════════════════════\n";

    return g_failCount > 0 ? 1 : 0;
}
