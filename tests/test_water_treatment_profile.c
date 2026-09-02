/**
 * @file test_water_treatment_profile.c
 * @brief Tests for Water Treatment (Industrial) domain profile integration.
 *
 * Architecture ref: Section 6 (vtable pattern), Section 9, Section 10.
 * Phase 6: Water Treatment as fourth domain profile — highest safety
 * stakes built so far (chemical dosing, direct risk of harm).
 */

#include "test_helpers/test_utils.h"
#include "../firmware/shared/rule_engine_core.h"
#include "../firmware/shared/sensor_validation.h"
#include "../firmware/shared/actuator_failsafe.h"
#include "../firmware/profiles/water_treatment/water_rules.h"
#include "../firmware/profiles/water_treatment/water_validation_bounds.h"
#include "../firmware/profiles/water_treatment/water_failsafe.h"
#include <string.h>

/* ---------- Test: vtable exists ---------- */
static int test_water_vtable_exists(void)
{
    const DomainProfileVTable_t *vtable = water_profile_get_vtable();
    TEST_ASSERT_NOT_NULL(vtable);
    TEST_ASSERT_NOT_NULL(vtable->getRuleTable);
    TEST_ASSERT_NOT_NULL(vtable->validateSensorReading);
    TEST_ASSERT_NOT_NULL(vtable->getFailSafeMode);
    TEST_ASSERT_NOT_NULL(vtable->executeAction);
    TEST_PASS();
}

/* ---------- Test: rule table has correct count ---------- */
static int test_water_rule_table_count(void)
{
    const RuleEntry_t *rules = NULL;
    uint8_t count = 0;
    water_getRuleTable(&rules, &count);

    TEST_ASSERT_NOT_NULL(rules);
    TEST_ASSERT(count >= 4);  /* chlorine safety, pH high, pH low, tank refill. */
    TEST_PASS();
}

/* ---------- Test: CRITICAL rules exist ---------- */
static int test_water_has_critical_rules(void)
{
    const RuleEntry_t *rules = NULL;
    uint8_t count = 0;
    water_getRuleTable(&rules, &count);

    uint8_t critical_count = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (rules[i].rule_class == RULE_CLASS_SAFETY_LOCKED) {
            critical_count++;
        }
    }
    TEST_ASSERT(critical_count >= 3);  /* chlorine + pH high + pH low. */
    TEST_PASS();
}

/* ---------- Test: OPERATIONAL rule exists ---------- */
static int test_water_has_operational_rule(void)
{
    const RuleEntry_t *rules = NULL;
    uint8_t count = 0;
    water_getRuleTable(&rules, &count);

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
static int test_water_rules_struct_size(void)
{
    /* Verify RuleEntry_t is exactly 16 bytes packed (Phase 6.1:
     * added range_low field, must not have grown the struct). */
    TEST_ASSERT_EQUAL(16, sizeof(RuleEntry_t));

    const RuleEntry_t *rules = NULL;
    uint8_t count = 0;
    water_getRuleTable(&rules, &count);

    for (uint8_t i = 0; i < count; i++) {
        TEST_ASSERT_EQUAL(16, sizeof(RuleEntry_t));
    }
    TEST_PASS();
}

/* ---------- Test: chlorine level bounds ---------- */
static int test_water_chlorine_bounds(void)
{
    const SensorValidationBounds_t *bounds = water_get_validation_bounds(WATER_METRIC_CHLORINE_LEVEL);
    TEST_ASSERT_NOT_NULL(bounds);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bounds->min_physical, 0.001f);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, bounds->max_physical, 0.001f);
    TEST_ASSERT(bounds->stuck_timeout_sec > 0);
    TEST_PASS();
}

/* ---------- Test: pH bounds ---------- */
static int test_water_ph_bounds(void)
{
    const SensorValidationBounds_t *bounds = water_get_validation_bounds(WATER_METRIC_PH);
    TEST_ASSERT_NOT_NULL(bounds);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bounds->min_physical, 0.001f);
    TEST_ASSERT_EQUAL_FLOAT(14.0f, bounds->max_physical, 0.001f);
    TEST_ASSERT(bounds->stuck_timeout_sec > 0);
    TEST_PASS();
}

/* ---------- Test: tank level bounds ---------- */
static int test_water_tank_level_bounds(void)
{
    const SensorValidationBounds_t *bounds = water_get_validation_bounds(WATER_METRIC_TANK_LEVEL);
    TEST_ASSERT_NOT_NULL(bounds);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bounds->min_physical, 0.001f);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, bounds->max_physical, 0.001f);
    TEST_ASSERT(bounds->stuck_timeout_sec > 0);
    TEST_PASS();
}

/* ---------- Test: flow rate bounds ---------- */
static int test_water_flow_rate_bounds(void)
{
    const SensorValidationBounds_t *bounds = water_get_validation_bounds(WATER_METRIC_FLOW_RATE);
    TEST_ASSERT_NOT_NULL(bounds);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bounds->min_physical, 0.001f);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, bounds->max_physical, 0.001f);
    TEST_ASSERT(bounds->stuck_timeout_sec > 0);
    TEST_PASS();
}

/* ---------- Test: unknown metric returns NULL ---------- */
static int test_water_unknown_metric_bounds(void)
{
    const SensorValidationBounds_t *bounds = water_get_validation_bounds(255);
    TEST_ASSERT_NULL(bounds);
    TEST_PASS();
}

/* ---------- Test: history buffers accessible ---------- */
static int test_water_history_buffers(void)
{
    SensorHistory_t *h;

    h = water_get_metric_history(WATER_METRIC_CHLORINE_LEVEL);
    TEST_ASSERT_NOT_NULL(h);

    h = water_get_metric_history(WATER_METRIC_PH);
    TEST_ASSERT_NOT_NULL(h);

    h = water_get_metric_history(WATER_METRIC_TANK_LEVEL);
    TEST_ASSERT_NOT_NULL(h);

    h = water_get_metric_history(WATER_METRIC_FLOW_RATE);
    TEST_ASSERT_NOT_NULL(h);

    h = water_get_metric_history(255);
    TEST_ASSERT_NULL(h);
    TEST_PASS();
}

/* ---------- Test: failsafe table registered ---------- */
static int test_water_failsafe_registered(void)
{
    actuator_failsafe_init();
    bool result = water_failsafe_register_all();
    TEST_ASSERT(result == true);

    /* Should have at least 3 actuators (dosing valve, main valve, pump). */
    TEST_ASSERT(actuator_failsafe_count() >= 3);
    TEST_PASS();
}

/* ---------- Test: chlorine safety rule (CRITICAL) ---------- */
static int test_water_chlorine_safety_rule(void)
{
    rule_engine_init();

    const DomainProfileVTable_t *vtable = water_profile_get_vtable();

    /* Chlorine level above 4.0 ppm should trigger safety rule. */
    SensorReading_t reading = {
        .node_id = 1,
        .metric_id = WATER_METRIC_CHLORINE_LEVEL,
        .value = 5.0f,  /* Above 4.0 ppm threshold. */
        .timestamp_ms = 1000
    };

    RuleEvaluationResult_t results[8];
    uint8_t count = rule_engine_evaluate(vtable, &reading, results, 8);
    TEST_ASSERT(count >= 1);

    bool found_chlorine = false;
    for (uint8_t i = 0; i < count; i++) {
        if (results[i].rule_id == WATER_RULE_CHLORINE_SAFETY) {
            found_chlorine = true;
            TEST_ASSERT(results[i].triggered == true);
            break;
        }
    }
    TEST_ASSERT(found_chlorine == true);
    TEST_PASS();
}

/* ---------- Test: pH HIGH safety rule (CRITICAL) ---------- */
static int test_water_ph_high_safety_rule(void)
{
    rule_engine_init();

    const DomainProfileVTable_t *vtable = water_profile_get_vtable();

    /* pH above 8.5 should trigger high-pH safety rule. */
    SensorReading_t reading = {
        .node_id = 1,
        .metric_id = WATER_METRIC_PH,
        .value = 9.0f,  /* Above 8.5 threshold. */
        .timestamp_ms = 1000
    };

    RuleEvaluationResult_t results[8];
    uint8_t count = rule_engine_evaluate(vtable, &reading, results, 8);
    TEST_ASSERT(count >= 1);

    bool found_ph_high = false;
    for (uint8_t i = 0; i < count; i++) {
        if (results[i].rule_id == WATER_RULE_PH_HIGH_SAFETY) {
            found_ph_high = true;
            TEST_ASSERT(results[i].triggered == true);
            break;
        }
    }
    TEST_ASSERT(found_ph_high == true);
    TEST_PASS();
}

/* ---------- Test: pH LOW safety rule (CRITICAL) ---------- */
static int test_water_ph_low_safety_rule(void)
{
    rule_engine_init();

    const DomainProfileVTable_t *vtable = water_profile_get_vtable();

    /* pH below 6.5 should trigger low-pH safety rule.
     * This is a physically valid reading (pH 6.0 is measurable)
     * that indicates a real chemical condition (acidic water),
     * NOT a sensor malfunction. */
    SensorReading_t reading = {
        .node_id = 1,
        .metric_id = WATER_METRIC_PH,
        .value = 6.0f,  /* Below 6.5 threshold — corrosion risk. */
        .timestamp_ms = 1000
    };

    RuleEvaluationResult_t results[8];
    uint8_t count = rule_engine_evaluate(vtable, &reading, results, 8);
    TEST_ASSERT(count >= 1);

    bool found_ph_low = false;
    for (uint8_t i = 0; i < count; i++) {
        if (results[i].rule_id == WATER_RULE_PH_LOW_SAFETY) {
            found_ph_low = true;
            TEST_ASSERT(results[i].triggered == true);
            break;
        }
    }
    TEST_ASSERT(found_ph_low == true);
    TEST_PASS();
}

/* ---------- Test: pH at safe level does NOT trigger any rule ---------- */
static int test_water_ph_safe_no_trigger(void)
{
    rule_engine_init();

    const DomainProfileVTable_t *vtable = water_profile_get_vtable();

    /* pH at 7.5 (within safe 6.5-8.5 range) — should NOT trigger. */
    SensorReading_t reading = {
        .node_id = 1,
        .metric_id = WATER_METRIC_PH,
        .value = 7.5f,  /* Within safe range. */
        .timestamp_ms = 1000
    };

    RuleEvaluationResult_t results[8];
    uint8_t count = rule_engine_evaluate(vtable, &reading, results, 8);

    for (uint8_t i = 0; i < count; i++) {
        if (results[i].rule_id == WATER_RULE_PH_HIGH_SAFETY ||
            results[i].rule_id == WATER_RULE_PH_LOW_SAFETY) {
            /* Neither pH rule should be triggered at safe pH. */
            TEST_ASSERT(results[i].triggered == false);
        }
    }
    TEST_PASS();
}

/* ---------- Test: physically implausible pH fails SENSOR VALIDATION, not rule ---------- */
static int test_water_ph_implausible_still_fails_validation(void)
{
    const DomainProfileVTable_t *vtable = water_profile_get_vtable();

    /* pH of -5 is physically impossible — this is a sensor malfunction,
     * NOT a chemical condition. It should fail sensor validation
     * (reject as implausible), NOT trigger a safety rule.
     *
     * This test proves the two paths remain distinct:
     * - pH 6.0 (physically valid, chemically unsafe) → triggers rule
     * - pH -5.0 (physically impossible) → fails sensor validation
     */
    SensorReading_t implausible = {
        .node_id = 1,
        .metric_id = WATER_METRIC_PH,
        .value = -5.0f,  /* Physically impossible — sensor malfunction. */
        .timestamp_ms = 1000
    };

    /* Sensor validation must reject this. */
    TEST_ASSERT(vtable->validateSensorReading(&implausible) == false);

    /* The rule engine should still be able to evaluate it (rules don't
     * check validation), but the production pipeline would never reach
     * the rule engine with an invalid reading. */
    RuleEvaluationResult_t results[8];
    (void)rule_engine_evaluate(vtable, &implausible, results, 8);

    /* The low-pH rule WILL trigger (-5.0 < 6.5 is true), but in
     * production this path is never reached because sensor validation
     * gates the reading first. We just verify the validation gate
     * works — that's the key test. */
    TEST_PASS();
}

/* ---------- Test: chlorine at safe level does NOT trigger ---------- */
static int test_water_chlorine_safe_no_trigger(void)
{
    rule_engine_init();

    const DomainProfileVTable_t *vtable = water_profile_get_vtable();

    /* Chlorine level at 2.0 ppm (within safe range) — should NOT trigger. */
    SensorReading_t reading = {
        .node_id = 1,
        .metric_id = WATER_METRIC_CHLORINE_LEVEL,
        .value = 2.0f,  /* Within 0.2-4.0 ppm safe range. */
        .timestamp_ms = 1000
    };

    RuleEvaluationResult_t results[8];
    uint8_t count = rule_engine_evaluate(vtable, &reading, results, 8);

    for (uint8_t i = 0; i < count; i++) {
        if (results[i].rule_id == WATER_RULE_CHLORINE_SAFETY) {
            /* If the chlorine rule appears, it must NOT be triggered. */
            TEST_ASSERT(results[i].triggered == false);
            break;
        }
    }
    /* Rule may or may not appear in results — either is acceptable
     * as long as it is not triggered. */
    TEST_PASS();
}

/* ---------- Test: vtable validate and execute ---------- */
static int test_water_vtable_validate_execute(void)
{
    const DomainProfileVTable_t *vtable = water_profile_get_vtable();

    /* Valid reading (within bounds). */
    SensorReading_t valid = { .node_id = 1, .metric_id = WATER_METRIC_CHLORINE_LEVEL, .value = 2.0f, .timestamp_ms = 1000 };
    TEST_ASSERT(vtable->validateSensorReading(&valid) == true);

    /* Invalid reading (out of bounds — above max). */
    SensorReading_t invalid_high = { .node_id = 1, .metric_id = WATER_METRIC_CHLORINE_LEVEL, .value = 15.0f, .timestamp_ms = 1000 };
    TEST_ASSERT(vtable->validateSensorReading(&invalid_high) == false);

    /* Invalid reading (out of bounds — below min). */
    SensorReading_t invalid_low = { .node_id = 1, .metric_id = WATER_METRIC_PH, .value = -1.0f, .timestamp_ms = 1000 };
    TEST_ASSERT(vtable->validateSensorReading(&invalid_low) == false);

    /* Unknown metric. */
    SensorReading_t unknown = { .node_id = 1, .metric_id = 255, .value = 5.0f, .timestamp_ms = 1000 };
    TEST_ASSERT(vtable->validateSensorReading(&unknown) == false);

    /* Execute action. */
    TEST_ASSERT(vtable->executeAction(WATER_ACTUATOR_DOSING_VALVE, 0) == true);
    TEST_PASS();
}

/* ---------- Test: dosing valve failsafe — FORCE_OFF on both power and comms loss ---------- */
static int test_water_dosing_valve_failsafe(void)
{
    uint8_t power_mode = water_getFailSafeMode(WATER_ACTUATOR_DOSING_VALVE, true);
    uint8_t comms_mode = water_getFailSafeMode(WATER_ACTUATOR_DOSING_VALVE, false);

    /* Dosing valve: FORCE_OFF on both — unmonitored dosing is dangerous.
     * Fail-closed under uncertainty (Phase 5.2 principle). */
    TEST_ASSERT_EQUAL(FAILSAFE_FORCE_OFF, power_mode);
    TEST_ASSERT_EQUAL(FAILSAFE_FORCE_OFF, comms_mode);
    TEST_PASS();
}

/* ---------- Test: main supply valve failsafe — DE_ENERGIZE on power loss ---------- */
static int test_water_main_valve_failsafe(void)
{
    uint8_t power_mode = water_getFailSafeMode(WATER_ACTUATOR_MAIN_SUPPLY_VALVE, true);
    uint8_t comms_mode = water_getFailSafeMode(WATER_ACTUATOR_MAIN_SUPPLY_VALVE, false);

    /* Main valve: DE_ENERGIZE (normally-closed spring-return) on power loss. */
    TEST_ASSERT_EQUAL(FAILSAFE_DE_ENERGIZE, power_mode);
    /* Comms loss: HOLD_LAST (timeout triggers DE_ENERGIZE later). */
    TEST_ASSERT_EQUAL(FAILSAFE_HOLD_LAST, comms_mode);

    /* Power and comms modes MUST be different. */
    TEST_ASSERT(power_mode != comms_mode);
    TEST_PASS();
}

/* ---------- Test: circulation pump failsafe — FORCE_OFF on power loss ---------- */
static int test_water_circ_pump_failsafe(void)
{
    uint8_t power_mode = water_getFailSafeMode(WATER_ACTUATOR_CIRCULATION_PUMP, true);
    uint8_t comms_mode = water_getFailSafeMode(WATER_ACTUATOR_CIRCULATION_PUMP, false);

    /* Pump: FORCE_OFF on power loss. */
    TEST_ASSERT_EQUAL(FAILSAFE_FORCE_OFF, power_mode);
    /* Comms loss: HOLD_LAST (timeout triggers FORCE_OFF later). */
    TEST_ASSERT_EQUAL(FAILSAFE_HOLD_LAST, comms_mode);
    TEST_PASS();
}

/* ---------- Test: comms timeout values ---------- */
static int test_water_comms_timeouts(void)
{
    uint16_t dosing_timeout = water_get_comms_timeout(WATER_ACTUATOR_DOSING_VALVE);
    uint16_t main_timeout = water_get_comms_timeout(WATER_ACTUATOR_MAIN_SUPPLY_VALVE);
    uint16_t pump_timeout = water_get_comms_timeout(WATER_ACTUATOR_CIRCULATION_PUMP);

    TEST_ASSERT_EQUAL(10, dosing_timeout);
    TEST_ASSERT_EQUAL(60, main_timeout);
    TEST_ASSERT_EQUAL(120, pump_timeout);
    TEST_PASS();
}

/* ---------- Main ---------- */
int main(void)
{
    int _total = 0, _passed = 0, _failed = 0;

    printf("=== test_water_treatment_profile ===\n");
    RUN_TEST(test_water_vtable_exists);
    RUN_TEST(test_water_rule_table_count);
    RUN_TEST(test_water_has_critical_rules);
    RUN_TEST(test_water_has_operational_rule);
    RUN_TEST(test_water_rules_struct_size);
    RUN_TEST(test_water_chlorine_bounds);
    RUN_TEST(test_water_ph_bounds);
    RUN_TEST(test_water_tank_level_bounds);
    RUN_TEST(test_water_flow_rate_bounds);
    RUN_TEST(test_water_unknown_metric_bounds);
    RUN_TEST(test_water_history_buffers);
    RUN_TEST(test_water_failsafe_registered);
    RUN_TEST(test_water_chlorine_safety_rule);
    RUN_TEST(test_water_ph_high_safety_rule);
    RUN_TEST(test_water_ph_low_safety_rule);
    RUN_TEST(test_water_ph_safe_no_trigger);
    RUN_TEST(test_water_ph_implausible_still_fails_validation);
    RUN_TEST(test_water_chlorine_safe_no_trigger);
    RUN_TEST(test_water_vtable_validate_execute);
    RUN_TEST(test_water_dosing_valve_failsafe);
    RUN_TEST(test_water_main_valve_failsafe);
    RUN_TEST(test_water_circ_pump_failsafe);
    RUN_TEST(test_water_comms_timeouts);

    PRINT_TEST_SUMMARY();
}
