/**
 * @file test_actuator_failsafe.c
 * @brief Tests for actuator fail-safe mechanism.
 *
 * Architecture ref: Section 10.
 */

#include "test_helpers/test_utils.h"
#include "../firmware/shared/actuator_failsafe.h"
#include <string.h>

/* ---------- Test: init ---------- */
static int test_actuator_failsafe_init(void)
{
    TEST_ASSERT(actuator_failsafe_init() == true);
    TEST_PASS();
}

/* ---------- Test: register entry ---------- */
static int test_actuator_failsafe_register(void)
{
    actuator_failsafe_init();

    ActuatorFailSafeEntry_t entry = {
        .actuator_id = 1,
        .criticality = ACTUATOR_SAFETY_CRITICAL,
        .power_loss_mode = FAILSAFE_FORCE_SAFE_POS,
        .comms_loss_mode = FAILSAFE_HOLD_LAST,
        .comms_timeout_sec = 30
    };

    TEST_ASSERT(actuator_failsafe_register(&entry) == true);
    TEST_ASSERT_EQUAL(1, actuator_failsafe_count());
    TEST_PASS();
}

/* ---------- Test: lookup entry ---------- */
static int test_actuator_failsafe_lookup(void)
{
    actuator_failsafe_init();

    ActuatorFailSafeEntry_t entry = {
        .actuator_id = 5,
        .criticality = ACTUATOR_SAFETY_CRITICAL,
        .power_loss_mode = FAILSAFE_FORCE_OFF,
        .comms_loss_mode = FAILSAFE_HOLD_LAST,
        .comms_timeout_sec = 60
    };
    actuator_failsafe_register(&entry);

    ActuatorFailSafeEntry_t found;
    TEST_ASSERT(actuator_failsafe_lookup(5, true, &found) == true);
    TEST_ASSERT_EQUAL(5, found.actuator_id);
    TEST_ASSERT_EQUAL(FAILSAFE_FORCE_OFF, found.power_loss_mode);
    TEST_ASSERT_EQUAL(FAILSAFE_HOLD_LAST, found.comms_loss_mode);
    TEST_ASSERT_EQUAL(60, found.comms_timeout_sec);

    /* Not found. */
    TEST_ASSERT(actuator_failsafe_lookup(99, true, &found) == false);
    TEST_PASS();
}

/* ---------- Test: execute power loss ---------- */
static int test_actuator_failsafe_execute_power_loss(void)
{
    actuator_failsafe_init();

    ActuatorFailSafeEntry_t entry = {
        .actuator_id = 1,
        .criticality = ACTUATOR_SAFETY_CRITICAL,
        .power_loss_mode = FAILSAFE_FORCE_SAFE_POS,
        .comms_loss_mode = FAILSAFE_HOLD_LAST,
        .comms_timeout_sec = 30
    };
    actuator_failsafe_register(&entry);

    /* Power loss — should execute immediately. */
    TEST_ASSERT(actuator_failsafe_execute(1, true, 0) == true);
    TEST_PASS();
}

/* ---------- Test: execute comms loss before timeout ---------- */
static int test_actuator_failsafe_comms_loss_before_timeout(void)
{
    actuator_failsafe_init();

    ActuatorFailSafeEntry_t entry = {
        .actuator_id = 1,
        .criticality = ACTUATOR_SAFETY_CRITICAL,
        .power_loss_mode = FAILSAFE_FORCE_SAFE_POS,
        .comms_loss_mode = FAILSAFE_HOLD_LAST,
        .comms_timeout_sec = 60
    };
    actuator_failsafe_register(&entry);

    /* Comms loss at 30s — before 60s timeout — should hold last. */
    TEST_ASSERT(actuator_failsafe_execute(1, false, 30) == true);
    TEST_PASS();
}

/* ---------- Test: execute comms loss after timeout ---------- */
static int test_actuator_failsafe_comms_loss_after_timeout(void)
{
    actuator_failsafe_init();

    ActuatorFailSafeEntry_t entry = {
        .actuator_id = 1,
        .criticality = ACTUATOR_SAFETY_CRITICAL,
        .power_loss_mode = FAILSAFE_FORCE_SAFE_POS,
        .comms_loss_mode = FAILSAFE_HOLD_LAST,
        .comms_timeout_sec = 60
    };
    actuator_failsafe_register(&entry);

    /* Comms loss at 90s — after 60s timeout — should trigger comms_loss_mode. */
    TEST_ASSERT(actuator_failsafe_execute(1, false, 90) == true);
    TEST_PASS();
}

/* ---------- Test: power loss vs comms loss distinction ---------- */
static int test_power_vs_comms_distinction(void)
{
    actuator_failsafe_init();

    ActuatorFailSafeEntry_t entry = {
        .actuator_id = 1,
        .criticality = ACTUATOR_SAFETY_CRITICAL,
        .power_loss_mode = FAILSAFE_FORCE_OFF,
        .comms_loss_mode = FAILSAFE_HOLD_LAST,
        .comms_timeout_sec = 60
    };
    actuator_failsafe_register(&entry);

    ActuatorFailSafeEntry_t found;

    /* Power loss lookup returns power_loss_mode. */
    actuator_failsafe_lookup(1, true, &found);
    TEST_ASSERT_EQUAL(FAILSAFE_FORCE_OFF, found.power_loss_mode);

    /* Comms loss lookup returns comms_loss_mode. */
    actuator_failsafe_lookup(1, false, &found);
    TEST_ASSERT_EQUAL(FAILSAFE_HOLD_LAST, found.comms_loss_mode);

    /* They MUST be different. */
    TEST_ASSERT(found.power_loss_mode != found.comms_loss_mode);
    TEST_PASS();
}

/* ---------- Test: register multiple ---------- */
static int test_actuator_failsafe_multiple(void)
{
    actuator_failsafe_init();

    ActuatorFailSafeEntry_t e1 = { .actuator_id = 1, .power_loss_mode = FAILSAFE_FORCE_OFF,
                                    .comms_loss_mode = FAILSAFE_HOLD_LAST, .comms_timeout_sec = 30 };
    ActuatorFailSafeEntry_t e2 = { .actuator_id = 2, .power_loss_mode = FAILSAFE_FORCE_OFF,
                                    .comms_loss_mode = FAILSAFE_FORCE_OFF, .comms_timeout_sec = 10 };

    actuator_failsafe_register(&e1);
    actuator_failsafe_register(&e2);
    TEST_ASSERT_EQUAL(2, actuator_failsafe_count());

    ActuatorFailSafeEntry_t found;
    TEST_ASSERT(actuator_failsafe_lookup(1, true, &found) == true);
    TEST_ASSERT_EQUAL(30, found.comms_timeout_sec);
    TEST_ASSERT(actuator_failsafe_lookup(2, true, &found) == true);
    TEST_ASSERT_EQUAL(10, found.comms_timeout_sec);
    TEST_PASS();
}

/* ---------- Test: not found execute ---------- */
static int test_actuator_failsafe_not_found(void)
{
    actuator_failsafe_init();
    TEST_ASSERT(actuator_failsafe_execute(99, true, 0) == false);
    TEST_PASS();
}

/* ---------- Test: update existing entry ---------- */
static int test_actuator_failsafe_update(void)
{
    actuator_failsafe_init();

    ActuatorFailSafeEntry_t e1 = { .actuator_id = 1, .power_loss_mode = FAILSAFE_FORCE_OFF,
                                    .comms_loss_mode = FAILSAFE_HOLD_LAST, .comms_timeout_sec = 30 };
    actuator_failsafe_register(&e1);

    ActuatorFailSafeEntry_t e2 = { .actuator_id = 1, .power_loss_mode = FAILSAFE_DE_ENERGIZE,
                                    .comms_loss_mode = FAILSAFE_FORCE_OFF, .comms_timeout_sec = 60 };
    actuator_failsafe_register(&e2);

    TEST_ASSERT_EQUAL(1, actuator_failsafe_count());  /* Still 1, updated. */

    ActuatorFailSafeEntry_t found;
    actuator_failsafe_lookup(1, true, &found);
    TEST_ASSERT_EQUAL(FAILSAFE_DE_ENERGIZE, found.power_loss_mode);
    TEST_PASS();
}

/* ---------- Main ---------- */
int main(void)
{
    int _total = 0, _passed = 0, _failed = 0;

    printf("=== test_actuator_failsafe ===\n");
    RUN_TEST(test_actuator_failsafe_init);
    RUN_TEST(test_actuator_failsafe_register);
    RUN_TEST(test_actuator_failsafe_lookup);
    RUN_TEST(test_actuator_failsafe_execute_power_loss);
    RUN_TEST(test_actuator_failsafe_comms_loss_before_timeout);
    RUN_TEST(test_actuator_failsafe_comms_loss_after_timeout);
    RUN_TEST(test_power_vs_comms_distinction);
    RUN_TEST(test_actuator_failsafe_multiple);
    RUN_TEST(test_actuator_failsafe_not_found);
    RUN_TEST(test_actuator_failsafe_update);

    PRINT_TEST_SUMMARY();
}
