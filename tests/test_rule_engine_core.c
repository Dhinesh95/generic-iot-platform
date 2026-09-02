/**
 * @file test_rule_engine_core.c
 * @brief Tests for rule engine core — generic evaluation with vtable.
 *
 * Architecture ref: Section 6.
 */

#include "test_helpers/test_utils.h"
#include "../firmware/shared/rule_engine_core.h"
#include <string.h>

/* ---------- Test data ---------- */

static const RuleEntry_t s_test_rules[] = {
    {
        .rule_id = 1,
        .priority = 0,
        .threshold = 50.0f,
        .action_type = RULE_ACTION_SET_ACTUATOR,
        .actuator_id = 1,
        .metric_id = 10,
        .rule_class = RULE_CLASS_SAFETY_LOCKED,
        .interlock_id = 0,
        .comparison_type = RULE_COMPARE_ABOVE,
        .reserved = {0, 0, 0}
    },
    {
        .rule_id = 2,
        .priority = 5,
        .threshold = 100.0f,
        .action_type = RULE_ACTION_SET_ACTUATOR,
        .actuator_id = 2,
        .metric_id = 10,
        .rule_class = RULE_CLASS_OPERATIONAL,
        .interlock_id = 0,
        .comparison_type = RULE_COMPARE_ABOVE,
        .reserved = {0, 0, 0}
    },
    {
        .rule_id = 3,
        .priority = 3,
        .threshold = 200.0f,
        .action_type = RULE_ACTION_TRIGGER_ALERT,
        .actuator_id = 0,
        .metric_id = 20,
        .rule_class = RULE_CLASS_OPERATIONAL,
        .interlock_id = 0,
        .comparison_type = RULE_COMPARE_ABOVE,
        .reserved = {0, 0, 0}
    }
};

#define TEST_RULE_COUNT (sizeof(s_test_rules) / sizeof(s_test_rules[0]))

static void test_getRuleTable(const RuleEntry_t **out_entries, uint8_t *out_count)
{
    if (out_entries) *out_entries = s_test_rules;
    if (out_count) *out_count = (uint8_t)TEST_RULE_COUNT;
}

static bool test_validateSensorReading(const SensorReading_t *reading)
{
    (void)reading;
    return true;
}

static uint8_t test_getFailSafeMode(uint8_t actuator_id, bool is_power_loss)
{
    (void)actuator_id;
    (void)is_power_loss;
    return 0;
}

static bool test_executeAction(uint8_t actuator_id, uint8_t state)
{
    (void)actuator_id;
    (void)state;
    return true;
}

static const DomainProfileVTable_t s_test_vtable = {
    .getRuleTable = test_getRuleTable,
    .validateSensorReading = test_validateSensorReading,
    .getFailSafeMode = test_getFailSafeMode,
    .executeAction = test_executeAction
};

/* ---------- Test: init ---------- */
static int test_rule_engine_init(void)
{
    TEST_ASSERT(rule_engine_init() == true);
    TEST_PASS();
}

/* ---------- Test: evaluate — no match ---------- */
static int test_rule_engine_no_match(void)
{
    rule_engine_init();

    SensorReading_t reading = {
        .node_id = 1,
        .metric_id = 99,  /* No rules for this metric. */
        .value = 75.0f,
        .timestamp_ms = 1000
    };

    RuleEvaluationResult_t results[8];
    uint8_t count = rule_engine_evaluate(&s_test_vtable, &reading, results, 8);
    TEST_ASSERT_EQUAL(0, count);
    TEST_PASS();
}

/* ---------- Test: evaluate — threshold match ---------- */
static int test_rule_engine_threshold_match(void)
{
    rule_engine_init();

    SensorReading_t reading = {
        .node_id = 1,
        .metric_id = 10,
        .value = 60.0f,  /* Above threshold of 50 for rule 1. */
        .timestamp_ms = 1000
    };

    RuleEvaluationResult_t results[8];
    uint8_t count = rule_engine_evaluate(&s_test_vtable, &reading, results, 8);
    /* Both rule 1 (threshold 50) and rule 2 (threshold 100) have metric_id 10.
       60 >= 50 triggers rule 1. 60 < 100 does not trigger rule 2. */
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL(1, results[0].rule_id);
    TEST_ASSERT(results[0].triggered == true);
    TEST_PASS();
}

/* ---------- Test: evaluate — multiple matches ---------- */
static int test_rule_engine_multiple_matches(void)
{
    rule_engine_init();

    SensorReading_t reading = {
        .node_id = 1,
        .metric_id = 10,
        .value = 150.0f,  /* Above both thresholds (50 and 100). */
        .timestamp_ms = 1000
    };

    RuleEvaluationResult_t results[8];
    uint8_t count = rule_engine_evaluate(&s_test_vtable, &reading, results, 8);
    TEST_ASSERT_EQUAL(2, count);
    TEST_PASS();
}

/* ---------- Test: find rule ---------- */
static int test_rule_engine_find_rule(void)
{
    rule_engine_init();

    const RuleEntry_t *rule = rule_engine_find_rule(&s_test_vtable, 1);
    TEST_ASSERT_NOT_NULL(rule);
    TEST_ASSERT_EQUAL(1, rule->rule_id);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, rule->threshold, 0.001f);

    /* Non-existent rule. */
    const RuleEntry_t *not_found = rule_engine_find_rule(&s_test_vtable, 999);
    TEST_ASSERT_NULL(not_found);
    TEST_PASS();
}

/* ---------- Test: safety-locked check ---------- */
static int test_rule_is_safety_locked(void)
{
    const RuleEntry_t *locked = &s_test_rules[0];
    const RuleEntry_t *operational = &s_test_rules[1];

    TEST_ASSERT(rule_is_safety_locked(locked) == true);
    TEST_ASSERT(rule_is_safety_locked(operational) == false);
    TEST_ASSERT(rule_is_safety_locked(NULL) == false);
    TEST_PASS();
}

/* ---------- Test: struct packing ---------- */
static int test_rule_entry_struct_size(void)
{
    /* RuleEntry_t must be exactly 16 bytes packed. */
    TEST_ASSERT_EQUAL(16, sizeof(RuleEntry_t));
    TEST_PASS();
}

/* ---------- Test: NULL parameters ---------- */
static int test_rule_engine_null_params(void)
{
    rule_engine_init();

    SensorReading_t reading = { .node_id = 1, .metric_id = 10, .value = 50.0f, .timestamp_ms = 1000 };
    RuleEvaluationResult_t results[8];

    TEST_ASSERT_EQUAL(0, rule_engine_evaluate(NULL, &reading, results, 8));
    TEST_ASSERT_EQUAL(0, rule_engine_evaluate(&s_test_vtable, NULL, results, 8));
    TEST_ASSERT_EQUAL(0, rule_engine_evaluate(&s_test_vtable, &reading, NULL, 8));
    TEST_ASSERT_NULL(rule_engine_find_rule(NULL, 1));
    TEST_PASS();
}

/* ---------- Main ---------- */
int main(void)
{
    int _total = 0, _passed = 0, _failed = 0;

    printf("=== test_rule_engine_core ===\n");
    RUN_TEST(test_rule_engine_init);
    RUN_TEST(test_rule_engine_no_match);
    RUN_TEST(test_rule_engine_threshold_match);
    RUN_TEST(test_rule_engine_multiple_matches);
    RUN_TEST(test_rule_engine_find_rule);
    RUN_TEST(test_rule_is_safety_locked);
    RUN_TEST(test_rule_entry_struct_size);
    RUN_TEST(test_rule_engine_null_params);

    PRINT_TEST_SUMMARY();
}
