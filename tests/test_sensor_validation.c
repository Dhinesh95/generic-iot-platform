/**
 * @file test_sensor_validation.c
 * @brief Tests for sensor validation / plausibility gate.
 *
 * Architecture ref: Section 9.
 * Requirement: SR-003 — Out-of-bounds sensor reading shall trigger
 *             fail-safe, never be used as valid.
 */

#include "test_helpers/test_utils.h"
#include "../firmware/shared/sensor_validation.h"
#include <string.h>

/* ---------- Test: init ---------- */
static int test_sensor_validation_init(void)
{
    TEST_ASSERT(sensor_validation_init() == true);
    TEST_PASS();
}

/* ---------- Test: valid reading passes ---------- */
static int test_sensor_valid_reading(void)
{
    sensor_validation_init();

    SensorValidationBounds_t bounds = {
        .min_physical = 0.0f,
        .max_physical = 100.0f,
        .max_rate_of_change = 10.0f,
        .stuck_timeout_sec = 60,
        .cross_check_node_id = 0
    };

    SensorHistory_t history;
    memset(&history, 0, sizeof(history));

    /* First reading — should be valid (only range check). */
    SensorValidationResult_t result = sensorValidate(
        1, 10, 50.0f, &bounds, &history, 1000);
    TEST_ASSERT(result == SENSOR_VALID);
    TEST_PASS();
}

/* ---------- Test: out of physical range ---------- */
static int test_sensor_out_of_range(void)
{
    sensor_validation_init();

    SensorValidationBounds_t bounds = {
        .min_physical = 0.0f,
        .max_physical = 100.0f,
        .max_rate_of_change = 1000.0f,
        .stuck_timeout_sec = 0,
        .cross_check_node_id = 0
    };

    SensorHistory_t history;
    memset(&history, 0, sizeof(history));

    /* Below minimum. */
    SensorValidationResult_t result = sensorValidate(
        1, 10, -5.0f, &bounds, &history, 1000);
    TEST_ASSERT(result == SENSOR_OUT_OF_PHYSICAL_RANGE);

    /* Above maximum. */
    result = sensorValidate(
        1, 10, 150.0f, &bounds, &history, 2000);
    TEST_ASSERT(result == SENSOR_OUT_OF_PHYSICAL_RANGE);
    TEST_PASS();
}

/* ---------- Test: rate of change exceeded ---------- */
static int test_sensor_rate_exceeded(void)
{
    sensor_validation_init();

    SensorValidationBounds_t bounds = {
        .min_physical = 0.0f,
        .max_physical = 1000.0f,
        .max_rate_of_change = 10.0f,
        .stuck_timeout_sec = 0,
        .cross_check_node_id = 0
    };

    SensorHistory_t history;
    memset(&history, 0, sizeof(history));

    /* Record first reading. */
    sensor_history_record(&history, 1, 10, 50.0f, 1000);

    /* Second reading 1 second later, delta = 20 (rate = 20/sec > 10). */
    SensorValidationResult_t result = sensorValidate(
        1, 10, 70.0f, &bounds, &history, 2000);
    TEST_ASSERT(result == SENSOR_RATE_EXCEEDED);
    TEST_PASS();
}

/* ---------- Test: stuck detection ---------- */
static int test_sensor_stuck(void)
{
    sensor_validation_init();

    SensorValidationBounds_t bounds = {
        .min_physical = 0.0f,
        .max_physical = 100.0f,
        .max_rate_of_change = 1000.0f,
        .stuck_timeout_sec = 10,  /* 10 seconds. */
        .cross_check_node_id = 0
    };

    SensorHistory_t history;
    memset(&history, 0, sizeof(history));

    /* Record first reading. */
    sensor_history_record(&history, 1, 10, 50.0f, 1000);

    /* Same value at time 5000 (5 seconds) — should still be valid (not stuck yet). */
    SensorValidationResult_t result = sensorValidate(
        1, 10, 50.0f, &bounds, &history, 6000);
    TEST_ASSERT(result == SENSOR_VALID);

    /* Same value at time 12000 (12 seconds since last change) — stuck. */
    result = sensorValidate(
        1, 10, 50.0f, &bounds, &history, 13000);
    TEST_ASSERT(result == SENSOR_STUCK);
    TEST_PASS();
}

/* ---------- Test: history recording ---------- */
static int test_sensor_history_record(void)
{
    SensorHistory_t history;
    memset(&history, 0, sizeof(history));

    sensor_history_record(&history, 1, 10, 25.0f, 1000);
    TEST_ASSERT(history.initialised == true);
    TEST_ASSERT_EQUAL(1, history.count);
    TEST_ASSERT_EQUAL_FLOAT(25.0f, history.values[0], 0.001f);

    sensor_history_record(&history, 1, 10, 50.0f, 2000);
    TEST_ASSERT_EQUAL(2, history.count);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, history.last_value, 0.001f);
    TEST_PASS();
}

/* ---------- Test: history reset ---------- */
static int test_sensor_history_reset(void)
{
    SensorHistory_t history;
    memset(&history, 0, sizeof(history));

    sensor_history_record(&history, 1, 10, 25.0f, 1000);
    TEST_ASSERT(history.initialised == true);

    sensor_history_reset(&history);
    TEST_ASSERT(history.initialised == false);
    TEST_ASSERT_EQUAL(0, history.count);
    TEST_PASS();
}

/* ---------- Test: rate OK ---------- */
static int test_sensor_rate_ok(void)
{
    sensor_validation_init();

    SensorValidationBounds_t bounds = {
        .min_physical = 0.0f,
        .max_physical = 1000.0f,
        .max_rate_of_change = 10.0f,
        .stuck_timeout_sec = 0,
        .cross_check_node_id = 0
    };

    SensorHistory_t history;
    memset(&history, 0, sizeof(history));

    sensor_history_record(&history, 1, 10, 50.0f, 1000);

    /* Delta = 5, rate = 5/sec < 10. OK. */
    SensorValidationResult_t result = sensorValidate(
        1, 10, 55.0f, &bounds, &history, 2000);
    TEST_ASSERT(result == SENSOR_VALID);
    TEST_PASS();
}

/* ---------- Test: NULL params ---------- */
static int test_sensor_null_params(void)
{
    sensor_validation_init();

    SensorValidationBounds_t bounds = {
        .min_physical = 0.0f, .max_physical = 100.0f,
        .max_rate_of_change = 100.0f, .stuck_timeout_sec = 0,
        .cross_check_node_id = 0
    };

    SensorValidationResult_t result = sensorValidate(
        1, 10, 50.0f, NULL, NULL, 1000);
    TEST_ASSERT(result == SENSOR_OUT_OF_PHYSICAL_RANGE);

    (void)bounds;
    TEST_PASS();
}

/* ---------- Main ---------- */
int main(void)
{
    int _total = 0, _passed = 0, _failed = 0;

    printf("=== test_sensor_validation ===\n");
    RUN_TEST(test_sensor_validation_init);
    RUN_TEST(test_sensor_valid_reading);
    RUN_TEST(test_sensor_out_of_range);
    RUN_TEST(test_sensor_rate_exceeded);
    RUN_TEST(test_sensor_stuck);
    RUN_TEST(test_sensor_history_record);
    RUN_TEST(test_sensor_history_reset);
    RUN_TEST(test_sensor_rate_ok);
    RUN_TEST(test_sensor_null_params);

    PRINT_TEST_SUMMARY();
}
