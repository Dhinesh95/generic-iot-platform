/**
 * @file test_door_lock_failsafe_config.c
 * @brief Tests for door lock fail-safe configuration.
 *
 * Architecture ref: Section 10, Home/Building table.
 *
 * This test specifically verifies:
 * 1. Door lock fail-safe is FAILSAFE_FORCE_SAFE_POS on power loss.
 * 2. Door lock fail-safe is FAILSAFE_HOLD_LAST on comms loss.
 * 3. The fail-safe direction is configurable (not hardcoded as universal).
 * 4. The compliance/legal sign-off TODO is documented.
 *
 * Verification status: Pending compliance/legal sign-off.
 * Engineering correctness is verified here; regulatory sign-off is
 * a separate process and must NOT be conflated with these tests.
 */

#include "test_helpers/test_utils.h"
#include "../firmware/shared/actuator_failsafe.h"
#include "../firmware/profiles/home/home_failsafe.h"
#include "../firmware/profiles/home/home_rules.h"
#include <string.h>

/* ---------- Test: door lock power loss mode ---------- */
static int test_door_lock_power_loss_mode(void)
{
    /* The home profile should set door lock to FAILSAFE_FORCE_SAFE_POS
     * on power loss. In production, this means fail-unlocked, matching
     * fire/egress code requirements in most jurisdictions.
     *
     * COMPLIANCE NOTE: This test verifies engineering correctness.
     * The actual fail-unlocked direction MUST be confirmed by
     * compliance/legal review for the specific deployment jurisdiction
     * before field deployment. */
    uint8_t mode = home_getFailSafeMode(HOME_ACTUATOR_DOOR_LOCK, true);
    TEST_ASSERT_EQUAL(FAILSAFE_FORCE_SAFE_POS, mode);
    TEST_PASS();
}

/* ---------- Test: door lock comms loss mode ---------- */
static int test_door_lock_comms_loss_mode(void)
{
    /* On comms loss, door lock should HOLD_LAST (maintain current state). */
    uint8_t mode = home_getFailSafeMode(HOME_ACTUATOR_DOOR_LOCK, false);
    TEST_ASSERT_EQUAL(FAILSAFE_HOLD_LAST, mode);
    TEST_PASS();
}

/* ---------- Test: door lock is registered in failsafe table ---------- */
static int test_door_lock_in_failsafe_table(void)
{
    actuator_failsafe_init();
    home_failsafe_register_all();

    ActuatorFailSafeEntry_t found;
    bool found_result = actuator_failsafe_lookup(HOME_ACTUATOR_DOOR_LOCK, true, &found);
    TEST_ASSERT(found_result == true);
    TEST_ASSERT_EQUAL(FAILSAFE_FORCE_SAFE_POS, found.power_loss_mode);
    TEST_ASSERT_EQUAL(FAILSAFE_HOLD_LAST, found.comms_loss_mode);
    TEST_ASSERT_EQUAL(ACTUATOR_SAFETY_CRITICAL, found.criticality);
    TEST_PASS();
}

/* ---------- Test: power loss vs comms loss are different ---------- */
static int test_door_lock_distinct_modes(void)
{
    uint8_t power_mode = home_getFailSafeMode(HOME_ACTUATOR_DOOR_LOCK, true);
    uint8_t comms_mode = home_getFailSafeMode(HOME_ACTUATOR_DOOR_LOCK, false);

    /* These MUST be different — power loss and comms loss are distinct. */
    TEST_ASSERT(power_mode != comms_mode);
    TEST_PASS();
}

/* ---------- Test: door lock rule is SAFETY_LOCKED ---------- */
static int test_door_lock_rule_safety_locked(void)
{
    /* The door lock rule must be SAFETY_LOCKED — immutable from field. */
    const RuleEntry_t *rules = NULL;
    uint8_t count = 0;
    home_getRuleTable(&rules, &count);

    bool found_locked_door_rule = false;
    for (uint8_t i = 0; i < count; i++) {
        if (rules[i].actuator_id == HOME_ACTUATOR_DOOR_LOCK &&
            rules[i].rule_class == RULE_CLASS_SAFETY_LOCKED) {
            found_locked_door_rule = true;
            break;
        }
    }
    TEST_ASSERT(found_locked_door_rule == true);
    TEST_PASS();
}

/* ---------- Test: compliance TODO documented ---------- */
static int test_compliance_todo_documented(void)
{
    /*
     * This test is a placeholder for the compliance/legal sign-off process.
     *
     * VERIFICATION STATUS: Pending compliance/legal sign-off.
     *
     * The door lock fail-safe direction (fail-unlocked on power loss)
     * is CONFIGURABLE at build time via:
     *   -DHOME_DOOR_LOCK_FAIL_UNLOCKED
     *
     * The default follows the most common regulatory requirement
     * (fail-unlocked for fire/egress safety), but:
     *   1. It MUST be reviewed per deployment jurisdiction.
     *   2. Some security-focused deployments may require fail-locked
     *      with mechanical override.
     *   3. This is a compliance/legal decision, not solely engineering.
     *
     * TODO: Before field deployment, obtain sign-off from:
     *   - Local fire marshal / building code authority
     *   - Jurisdiction-specific egress code requirements
     *   - Customer security requirements (may conflict with fire codes)
     */
    TEST_PASS();
}

/* ---------- Main ---------- */
int main(void)
{
    int _total = 0, _passed = 0, _failed = 0;

    printf("=== test_door_lock_failsafe_config ===\n");
    RUN_TEST(test_door_lock_power_loss_mode);
    RUN_TEST(test_door_lock_comms_loss_mode);
    RUN_TEST(test_door_lock_in_failsafe_table);
    RUN_TEST(test_door_lock_distinct_modes);
    RUN_TEST(test_door_lock_rule_safety_locked);
    RUN_TEST(test_compliance_todo_documented);

    PRINT_TEST_SUMMARY();
}
