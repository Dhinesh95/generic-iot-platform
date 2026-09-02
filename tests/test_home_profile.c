/**
 * @file test_home_profile.c
 * @brief Tests for Home/Building domain profile integration.
 *
 * Architecture ref: Section 6 (vtable pattern), Section 9, Section 10.
 */

#include "test_helpers/test_utils.h"
#include "../firmware/shared/rule_engine_core.h"
#include "../firmware/shared/sensor_validation.h"
#include "../firmware/shared/actuator_failsafe.h"
#include "../firmware/profiles/home/home_rules.h"
#include "../firmware/profiles/home/home_validation_bounds.h"
#include "../firmware/profiles/home/home_failsafe.h"
#include <string.h>

/* ---------- Test: vtable exists ---------- */
static int test_home_profile_vtable_exists(void)
{
    const DomainProfileVTable_t *vtable = home_profile_get_vtable();
    TEST_ASSERT_NOT_NULL(vtable);
    TEST_ASSERT_NOT_NULL(vtable->getRuleTable);
    TEST_ASSERT_NOT_NULL(vtable->validateSensorReading);
    TEST_ASSERT_NOT_NULL(vtable->getFailSafeMode);
    TEST_ASSERT_NOT_NULL(vtable->executeAction);
    TEST_PASS();
}

/* ---------- Test: rule table has correct count ---------- */
static int test_home_rule_table_count(void)
{
    const RuleEntry_t *rules = NULL;
    uint8_t count = 0;
    home_getRuleTable(&rules, &count);

    TEST_ASSERT_NOT_NULL(rules);
    TEST_ASSERT(count >= 3);  /* At minimum: door lock, irrigation, lighting. */
    TEST_PASS();
}

/* ---------- Test: CRITICAL rule exists ---------- */
static int test_home_has_critical_rule(void)
{
    const RuleEntry_t *rules = NULL;
    uint8_t count = 0;
    home_getRuleTable(&rules, &count);

    bool found_critical = false;
    for (uint8_t i = 0; i < count; i++) {
        if (rules[i].rule_class == RULE_CLASS_SAFETY_LOCKED) {
            found_critical = true;
            break;
        }
    }
    TEST_ASSERT(found_critical == true);
    TEST_PASS();
}

/* ---------- Test: OPERATIONAL rules exist ---------- */
static int test_home_has_operational_rules(void)
{
    const RuleEntry_t *rules = NULL;
    uint8_t count = 0;
    home_getRuleTable(&rules, &count);

    uint8_t op_count = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (rules[i].rule_class == RULE_CLASS_OPERATIONAL) {
            op_count++;
        }
    }
    TEST_ASSERT(op_count >= 2);  /* At minimum: irrigation + lighting. */
    TEST_PASS();
}

/* ---------- Test: all rules fit in 16 bytes ---------- */
static int test_home_rules_struct_size(void)
{
    const RuleEntry_t *rules = NULL;
    uint8_t count = 0;
    home_getRuleTable(&rules, &count);

    for (uint8_t i = 0; i < count; i++) {
        TEST_ASSERT_EQUAL(16, sizeof(RuleEntry_t));
    }
    TEST_PASS();
}

/* ---------- Test: validation bounds exist for door contact ---------- */
static int test_home_door_contact_bounds(void)
{
    const SensorValidationBounds_t *bounds = home_get_validation_bounds(HOME_METRIC_DOOR_CONTACT);
    TEST_ASSERT_NOT_NULL(bounds);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bounds->min_physical, 0.001f);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, bounds->max_physical, 0.001f);
    TEST_PASS();
}

/* ---------- Test: validation bounds exist for light level ---------- */
static int test_home_light_level_bounds(void)
{
    const SensorValidationBounds_t *bounds = home_get_validation_bounds(HOME_METRIC_LIGHT_LEVEL);
    TEST_ASSERT_NOT_NULL(bounds);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bounds->min_physical, 0.001f);
    TEST_ASSERT_EQUAL_FLOAT(1000.0f, bounds->max_physical, 0.001f);
    TEST_ASSERT(bounds->stuck_timeout_sec > 0);
    TEST_PASS();
}

/* ---------- Test: validation bounds exist for soil moisture ---------- */
static int test_home_soil_moisture_bounds(void)
{
    const SensorValidationBounds_t *bounds = home_get_validation_bounds(HOME_METRIC_SOIL_MOISTURE);
    TEST_ASSERT_NOT_NULL(bounds);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bounds->min_physical, 0.001f);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, bounds->max_physical, 0.001f);
    TEST_PASS();
}

/* ---------- Test: unknown metric returns NULL ---------- */
static int test_home_unknown_metric_bounds(void)
{
    const SensorValidationBounds_t *bounds = home_get_validation_bounds(255);
    TEST_ASSERT_NULL(bounds);
    TEST_PASS();
}

/* ---------- Test: history buffers accessible ---------- */
static int test_home_history_buffers(void)
{
    SensorHistory_t *h;

    h = home_get_metric_history(HOME_METRIC_DOOR_CONTACT);
    TEST_ASSERT_NOT_NULL(h);

    h = home_get_metric_history(HOME_METRIC_LIGHT_LEVEL);
    TEST_ASSERT_NOT_NULL(h);

    h = home_get_metric_history(HOME_METRIC_SOIL_MOISTURE);
    TEST_ASSERT_NOT_NULL(h);

    h = home_get_metric_history(255);
    TEST_ASSERT_NULL(h);
    TEST_PASS();
}

/* ---------- Test: failsafe table registered ---------- */
static int test_home_failsafe_registered(void)
{
    actuator_failsafe_init();
    bool result = home_failsafe_register_all();
    TEST_ASSERT(result == true);

    /* Should have at least 3 actuators (door lock, irrigation, light). */
    TEST_ASSERT(actuator_failsafe_count() >= 3);
    TEST_PASS();
}

/* ---------- Test: vtable evaluation integration ---------- */
static int test_home_vtable_evaluation(void)
{
    rule_engine_init();

    const DomainProfileVTable_t *vtable = home_profile_get_vtable();

    /* Trigger the door lock rule: security state armed (1.0 >= threshold 1.0). */
    SensorReading_t reading = {
        .node_id = 1,
        .metric_id = HOME_METRIC_SECURITY_STATE,
        .value = 1.0f,
        .timestamp_ms = 1000
    };

    RuleEvaluationResult_t results[8];
    uint8_t count = rule_engine_evaluate(vtable, &reading, results, 8);
    TEST_ASSERT(count >= 1);
    TEST_ASSERT_EQUAL(HOME_RULE_DOOR_LOCK_CRITICAL, results[0].rule_id);
    TEST_ASSERT(results[0].triggered == true);
    TEST_PASS();
}

/* ---------- Test: domain-specific validation via vtable ---------- */
static int test_home_vtable_validate(void)
{
    const DomainProfileVTable_t *vtable = home_profile_get_vtable();

    SensorReading_t valid = { .node_id = 1, .metric_id = 10, .value = 50.0f, .timestamp_ms = 1000 };
    TEST_ASSERT(vtable->validateSensorReading(&valid) == true);

    SensorReading_t invalid = { .node_id = 1, .metric_id = 10, .value = -10.0f, .timestamp_ms = 1000 };
    TEST_ASSERT(vtable->validateSensorReading(&invalid) == false);
    TEST_PASS();
}

/* ---------- Test: execute action via vtable ---------- */
static int test_home_vtable_execute(void)
{
    const DomainProfileVTable_t *vtable = home_profile_get_vtable();
    TEST_ASSERT(vtable->executeAction(HOME_ACTUATOR_DOOR_LOCK, ACTUATOR_STATE_ON) == true);
    TEST_ASSERT(vtable->executeAction(HOME_ACTUATOR_LIGHT_RELAY, ACTUATOR_STATE_OFF) == true);
    TEST_PASS();
}

/* ---------- Test: comms timeout values ---------- */
static int test_home_comms_timeouts(void)
{
    /* Irrigation should have a comms timeout. */
    uint16_t timeout = home_get_comms_timeout(HOME_ACTUATOR_IRRIGATION_VALVE);
    TEST_ASSERT(timeout > 0);

    /* Door lock should have 0 (HOLD_LAST indefinitely). */
    timeout = home_get_comms_timeout(HOME_ACTUATOR_DOOR_LOCK);
    TEST_ASSERT_EQUAL(0, timeout);
    TEST_PASS();
}

/* ---------- Main ---------- */
int main(void)
{
    int _total = 0, _passed = 0, _failed = 0;

    printf("=== test_home_profile ===\n");
    RUN_TEST(test_home_profile_vtable_exists);
    RUN_TEST(test_home_rule_table_count);
    RUN_TEST(test_home_has_critical_rule);
    RUN_TEST(test_home_has_operational_rules);
    RUN_TEST(test_home_rules_struct_size);
    RUN_TEST(test_home_door_contact_bounds);
    RUN_TEST(test_home_light_level_bounds);
    RUN_TEST(test_home_soil_moisture_bounds);
    RUN_TEST(test_home_unknown_metric_bounds);
    RUN_TEST(test_home_history_buffers);
    RUN_TEST(test_home_failsafe_registered);
    RUN_TEST(test_home_vtable_evaluation);
    RUN_TEST(test_home_vtable_validate);
    RUN_TEST(test_home_vtable_execute);
    RUN_TEST(test_home_comms_timeouts);

    PRINT_TEST_SUMMARY();
}
