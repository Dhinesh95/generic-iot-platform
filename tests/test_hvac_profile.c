/**
 * @file test_hvac_profile.c
 * @brief Tests for HVAC domain profile integration.
 *
 * Architecture ref: Section 6 (vtable pattern), Section 9, Section 10.
 * Phase 5: HVAC as third domain profile, proving core/profile separation
 * continues to hold with no shared-core changes.
 */

#include "test_helpers/test_utils.h"
#include "../firmware/shared/rule_engine_core.h"
#include "../firmware/shared/sensor_validation.h"
#include "../firmware/shared/actuator_failsafe.h"
#include "../firmware/profiles/hvac/hvac_rules.h"
#include "../firmware/profiles/hvac/hvac_validation_bounds.h"
#include "../firmware/profiles/hvac/hvac_failsafe.h"
#include <string.h>

/* ---------- Test: vtable exists ---------- */
static int test_hvac_vtable_exists(void)
{
    const DomainProfileVTable_t *vtable = hvac_profile_get_vtable();
    TEST_ASSERT_NOT_NULL(vtable);
    TEST_ASSERT_NOT_NULL(vtable->getRuleTable);
    TEST_ASSERT_NOT_NULL(vtable->validateSensorReading);
    TEST_ASSERT_NOT_NULL(vtable->getFailSafeMode);
    TEST_ASSERT_NOT_NULL(vtable->executeAction);
    TEST_PASS();
}

/* ---------- Test: rule table has correct count ---------- */
static int test_hvac_rule_table_count(void)
{
    const RuleEntry_t *rules = NULL;
    uint8_t count = 0;
    hvac_getRuleTable(&rules, &count);

    TEST_ASSERT_NOT_NULL(rules);
    TEST_ASSERT(count >= 3);  /* At minimum: compressor short-cycle, overtemp, setpoint. */
    TEST_PASS();
}

/* ---------- Test: CRITICAL rules exist ---------- */
static int test_hvac_has_critical_rules(void)
{
    const RuleEntry_t *rules = NULL;
    uint8_t count = 0;
    hvac_getRuleTable(&rules, &count);

    uint8_t critical_count = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (rules[i].rule_class == RULE_CLASS_SAFETY_LOCKED) {
            critical_count++;
        }
    }
    TEST_ASSERT(critical_count >= 2);  /* Compressor short-cycle + overtemp. */
    TEST_PASS();
}

/* ---------- Test: OPERATIONAL rule exists ---------- */
static int test_hvac_has_operational_rule(void)
{
    const RuleEntry_t *rules = NULL;
    uint8_t count = 0;
    hvac_getRuleTable(&rules, &count);

    bool found_operational = false;
    for (uint8_t i = 0; i < count; i++) {
        if (rules[i].rule_class == RULE_CLASS_OPERATIONAL) {
            found_operational = true;
            break;
        }
    }
    TEST_ASSERT(found_operational == true);
    TEST_PASS();
}

/* ---------- Test: all rules fit in 16 bytes ---------- */
static int test_hvac_rules_struct_size(void)
{
    const RuleEntry_t *rules = NULL;
    uint8_t count = 0;
    hvac_getRuleTable(&rules, &count);

    for (uint8_t i = 0; i < count; i++) {
        TEST_ASSERT_EQUAL(16, sizeof(RuleEntry_t));
    }
    TEST_PASS();
}

/* ---------- Test: supply temp bounds (indoor, tighter than Agriculture) ---------- */
static int test_hvac_supply_temp_bounds(void)
{
    const SensorValidationBounds_t *bounds = hvac_get_validation_bounds(HVAC_METRIC_SUPPLY_TEMP);
    TEST_ASSERT_NOT_NULL(bounds);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bounds->min_physical, 0.001f);
    TEST_ASSERT_EQUAL_FLOAT(60.0f, bounds->max_physical, 0.001f);
    TEST_ASSERT(bounds->stuck_timeout_sec > 0);
    TEST_PASS();
}

/* ---------- Test: return temp bounds ---------- */
static int test_hvac_return_temp_bounds(void)
{
    const SensorValidationBounds_t *bounds = hvac_get_validation_bounds(HVAC_METRIC_RETURN_TEMP);
    TEST_ASSERT_NOT_NULL(bounds);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bounds->min_physical, 0.001f);
    TEST_ASSERT_EQUAL_FLOAT(60.0f, bounds->max_physical, 0.001f);
    TEST_ASSERT(bounds->stuck_timeout_sec > 0);
    TEST_PASS();
}

/* ---------- Test: indoor humidity bounds ---------- */
static int test_hvac_indoor_humidity_bounds(void)
{
    const SensorValidationBounds_t *bounds = hvac_get_validation_bounds(HVAC_METRIC_INDOOR_HUMIDITY);
    TEST_ASSERT_NOT_NULL(bounds);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bounds->min_physical, 0.001f);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, bounds->max_physical, 0.001f);
    TEST_PASS();
}

/* ---------- Test: ambient temp bounds (indoor range) ---------- */
static int test_hvac_ambient_temp_bounds(void)
{
    const SensorValidationBounds_t *bounds = hvac_get_validation_bounds(HVAC_METRIC_AMBIENT_TEMP);
    TEST_ASSERT_NOT_NULL(bounds);
    /* Indoor range: -10 to +50 C (tighter than Agriculture's -40 to +60). */
    TEST_ASSERT_EQUAL_FLOAT(-10.0f, bounds->min_physical, 0.001f);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, bounds->max_physical, 0.001f);
    TEST_ASSERT(bounds->stuck_timeout_sec > 0);
    TEST_PASS();
}

/* ---------- Test: unknown metric returns NULL ---------- */
static int test_hvac_unknown_metric_bounds(void)
{
    const SensorValidationBounds_t *bounds = hvac_get_validation_bounds(255);
    TEST_ASSERT_NULL(bounds);
    TEST_PASS();
}

/* ---------- Test: history buffers accessible ---------- */
static int test_hvac_history_buffers(void)
{
    SensorHistory_t *h;

    h = hvac_get_metric_history(HVAC_METRIC_SUPPLY_TEMP);
    TEST_ASSERT_NOT_NULL(h);

    h = hvac_get_metric_history(HVAC_METRIC_RETURN_TEMP);
    TEST_ASSERT_NOT_NULL(h);

    h = hvac_get_metric_history(HVAC_METRIC_INDOOR_HUMIDITY);
    TEST_ASSERT_NOT_NULL(h);

    h = hvac_get_metric_history(HVAC_METRIC_AMBIENT_TEMP);
    TEST_ASSERT_NOT_NULL(h);

    h = hvac_get_metric_history(255);
    TEST_ASSERT_NULL(h);
    TEST_PASS();
}

/* ---------- Test: failsafe table registered ---------- */
static int test_hvac_failsafe_registered(void)
{
    actuator_failsafe_init();
    bool result = hvac_failsafe_register_all();
    TEST_ASSERT(result == true);

    /* Should have at least 3 actuators (compressor, damper, fan). */
    TEST_ASSERT(actuator_failsafe_count() >= 3);
    TEST_PASS();
}

/* ---------- Test: compressor post-run lockout rule (CRITICAL) ---------- */
static int test_hvac_compressor_post_run_lockout_rule(void)
{
    rule_engine_init();

    const DomainProfileVTable_t *vtable = hvac_profile_get_vtable();

    /* Supply temp above 45°C should trigger post-run lockout. */
    SensorReading_t reading = {
        .node_id = 1,
        .metric_id = HVAC_METRIC_SUPPLY_TEMP,
        .value = 48.0f,  /* Above 45°C threshold. */
        .timestamp_ms = 1000
    };

    RuleEvaluationResult_t results[8];
    uint8_t count = rule_engine_evaluate(vtable, &reading, results, 8);
    TEST_ASSERT(count >= 1);

    bool found_post_run = false;
    for (uint8_t i = 0; i < count; i++) {
        if (results[i].rule_id == HVAC_RULE_COMPRESSOR_POST_RUN_LOCKOUT) {
            found_post_run = true;
            TEST_ASSERT(results[i].triggered == true);
            break;
        }
    }
    TEST_ASSERT(found_post_run == true);
    TEST_PASS();
}

/* ---------- Test: short-cycle protection timing (time-based, stateful) ---------- */
static int test_hvac_short_cycle_timing(void)
{
    /* Reset state. */
    hvac_short_cycle_reset();

    /* Initially, no compressor-off recorded — restart should be permitted. */
    TEST_ASSERT(hvac_short_cycle_check(1000) == true);

    /* Record compressor turning off at t=1000. */
    hvac_short_cycle_record_off(1000);
    TEST_ASSERT_EQUAL(1000, hvac_short_cycle_get_last_off());

    /* Immediate restart at t=1001 (1 second later) — should be BLOCKED.
     * HVAC_MIN_COMPRESSOR_OFF_SEC is 300 (5 minutes). */
    TEST_ASSERT(hvac_short_cycle_check(1001) == false);

    /* Still blocked at t=1200 (200 seconds elapsed). */
    TEST_ASSERT(hvac_short_cycle_check(1200) == false);

    /* Still blocked at t=1299 (299 seconds elapsed — one second short). */
    TEST_ASSERT(hvac_short_cycle_check(1299) == false);

    /* Permitted at t=1300 (300 seconds elapsed = exactly HVAC_MIN_COMPRESSOR_OFF_SEC). */
    TEST_ASSERT(hvac_short_cycle_check(1300) == true);

    /* Still permitted at t=2000 (well past the window). */
    TEST_ASSERT(hvac_short_cycle_check(2000) == true);

    /* Confirm the constant is as expected. */
    TEST_ASSERT_EQUAL(300, hvac_short_cycle_get_min_off_sec());

    /* --- Clock-backward scenario (power brownout / reboot) --- */
    hvac_short_cycle_reset();
    hvac_short_cycle_record_off(2000);

    /* Clock went backward to t=1500 (500 sec before last-off) —
     * FAIL SAFE: restart must be BLOCKED, not permitted.
     * This is the power-brownout/reboot scenario where short-cycle
     * protection matters most. */
    TEST_ASSERT(hvac_short_cycle_check(1500) == false);

    /* Clock still unreliable — still blocked at t=1999. */
    TEST_ASSERT(hvac_short_cycle_check(1999) == false);

    /* Clock recovers and moves forward to t=2050 (only 50 seconds
     * after last-off). Fallback delay (120s) not yet elapsed — BLOCKED. */
    TEST_ASSERT(hvac_short_cycle_check(2050) == false);

    /* Clock at t=2100 (100 seconds after last-off). Fallback not yet
     * elapsed — still BLOCKED. */
    TEST_ASSERT(hvac_short_cycle_check(2100) == false);

    /* Clock at t=2120 (120 seconds after last-off = exactly
     * HVAC_CLOCK_UNRELIABLE_FALLBACK_SEC). Now PERMITTED. */
    TEST_ASSERT(hvac_short_cycle_check(2120) == true);

    /* Verify the fallback constant. */
    TEST_ASSERT_EQUAL(120, hvac_clock_unreliable_fallback_sec());

    TEST_PASS();
}

/* ---------- Test: vtable validate and execute ---------- */
static int test_hvac_vtable_validate_execute(void)
{
    const DomainProfileVTable_t *vtable = hvac_profile_get_vtable();

    /* Valid reading (within bounds). */
    SensorReading_t valid = { .node_id = 1, .metric_id = HVAC_METRIC_SUPPLY_TEMP, .value = 35.0f, .timestamp_ms = 1000 };
    TEST_ASSERT(vtable->validateSensorReading(&valid) == true);

    /* Invalid reading (out of bounds — below min). */
    SensorReading_t invalid_low = { .node_id = 1, .metric_id = HVAC_METRIC_SUPPLY_TEMP, .value = -5.0f, .timestamp_ms = 1000 };
    TEST_ASSERT(vtable->validateSensorReading(&invalid_low) == false);

    /* Invalid reading (out of bounds — above max). */
    SensorReading_t invalid_high = { .node_id = 1, .metric_id = HVAC_METRIC_SUPPLY_TEMP, .value = 70.0f, .timestamp_ms = 1000 };
    TEST_ASSERT(vtable->validateSensorReading(&invalid_high) == false);

    /* Unknown metric. */
    SensorReading_t unknown = { .node_id = 1, .metric_id = 255, .value = 25.0f, .timestamp_ms = 1000 };
    TEST_ASSERT(vtable->validateSensorReading(&unknown) == false);

    /* Execute action. */
    TEST_ASSERT(vtable->executeAction(HVAC_ACTUATOR_COMPRESSOR_RELAY, 1) == true);
    TEST_PASS();
}

/* ---------- Test: compressor failsafe — FORCE_OFF on both power and comms loss ---------- */
static int test_hvac_compressor_failsafe(void)
{
    uint8_t power_mode = hvac_getFailSafeMode(HVAC_ACTUATOR_COMPRESSOR_RELAY, true);
    uint8_t comms_mode = hvac_getFailSafeMode(HVAC_ACTUATOR_COMPRESSOR_RELAY, false);

    /* Compressor: FORCE_OFF on both — uncontrolled cycling is dangerous. */
    TEST_ASSERT_EQUAL(FAILSAFE_FORCE_OFF, power_mode);
    TEST_ASSERT_EQUAL(FAILSAFE_FORCE_OFF, comms_mode);
    TEST_PASS();
}

/* ---------- Test: damper failsafe — FORCE_SAFE_POS on power loss ---------- */
static int test_hvac_damper_failsafe(void)
{
    uint8_t power_mode = hvac_getFailSafeMode(HVAC_ACTUATOR_DAMPER_MOTOR, true);
    uint8_t comms_mode = hvac_getFailSafeMode(HVAC_ACTUATOR_DAMPER_MOTOR, false);

    /* Damper: FORCE_SAFE_POS (50% open) on power loss. */
    TEST_ASSERT_EQUAL(FAILSAFE_FORCE_SAFE_POS, power_mode);
    /* Comms loss: HOLD_LAST (timeout triggers FORCE_SAFE_POS later). */
    TEST_ASSERT_EQUAL(FAILSAFE_HOLD_LAST, comms_mode);

    /* Power and comms modes MUST be different. */
    TEST_ASSERT(power_mode != comms_mode);
    TEST_PASS();
}

/* ---------- Test: fan failsafe — FORCE_OFF on power loss ---------- */
static int test_hvac_fan_failsafe(void)
{
    uint8_t power_mode = hvac_getFailSafeMode(HVAC_ACTUATOR_FAN_MOTOR, true);
    uint8_t comms_mode = hvac_getFailSafeMode(HVAC_ACTUATOR_FAN_MOTOR, false);

    /* Fan: FORCE_OFF on power loss. */
    TEST_ASSERT_EQUAL(FAILSAFE_FORCE_OFF, power_mode);
    /* Comms loss: HOLD_LAST (timeout triggers FORCE_OFF later). */
    TEST_ASSERT_EQUAL(FAILSAFE_HOLD_LAST, comms_mode);
    TEST_PASS();
}

/* ---------- Test: comms timeout values ---------- */
static int test_hvac_comms_timeouts(void)
{
    uint16_t compressor_timeout = hvac_get_comms_timeout(HVAC_ACTUATOR_COMPRESSOR_RELAY);
    uint16_t damper_timeout = hvac_get_comms_timeout(HVAC_ACTUATOR_DAMPER_MOTOR);
    uint16_t fan_timeout = hvac_get_comms_timeout(HVAC_ACTUATOR_FAN_MOTOR);

    TEST_ASSERT_EQUAL(30, compressor_timeout);
    TEST_ASSERT_EQUAL(300, damper_timeout);
    TEST_ASSERT_EQUAL(300, fan_timeout);
    TEST_PASS();
}

/* ---------- Main ---------- */
int main(void)
{
    int _total = 0, _passed = 0, _failed = 0;

    printf("=== test_hvac_profile ===\n");
    RUN_TEST(test_hvac_vtable_exists);
    RUN_TEST(test_hvac_rule_table_count);
    RUN_TEST(test_hvac_has_critical_rules);
    RUN_TEST(test_hvac_has_operational_rule);
    RUN_TEST(test_hvac_rules_struct_size);
    RUN_TEST(test_hvac_supply_temp_bounds);
    RUN_TEST(test_hvac_return_temp_bounds);
    RUN_TEST(test_hvac_indoor_humidity_bounds);
    RUN_TEST(test_hvac_ambient_temp_bounds);
    RUN_TEST(test_hvac_unknown_metric_bounds);
    RUN_TEST(test_hvac_history_buffers);
    RUN_TEST(test_hvac_failsafe_registered);
    RUN_TEST(test_hvac_compressor_post_run_lockout_rule);
    RUN_TEST(test_hvac_short_cycle_timing);
    RUN_TEST(test_hvac_vtable_validate_execute);
    RUN_TEST(test_hvac_compressor_failsafe);
    RUN_TEST(test_hvac_damper_failsafe);
    RUN_TEST(test_hvac_fan_failsafe);
    RUN_TEST(test_hvac_comms_timeouts);

    PRINT_TEST_SUMMARY();
}
