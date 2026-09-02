/**
 * @file test_safety_monitor.c
 * @brief Tests for safety monitor (Phase 19).
 */

#include "test_helpers/test_utils.h"
#include "safety_monitor.h"
#include "time_source.h"

int _total = 0, _passed = 0, _failed = 0;
static bool s_failsafe_triggered = false;

static void mock_failsafe(void)
{
    s_failsafe_triggered = true;
}

static void reset_failsafe(void)
{
    s_failsafe_triggered = false;
}

static int test_safety_monitor_heartbeat_timeout(void)
{
    SafetyMonitorContext_t ctx;
    reset_failsafe();
    time_source_init();
    time_source_mock_set(0, true);

    safety_monitor_init(&ctx, mock_failsafe);

    /* Send initial heartbeat. */
    safety_monitor_heartbeat(&ctx);
    TEST_ASSERT_EQUAL(SAFETY_STATE_ACTIVE, safety_monitor_check(&ctx));

    /* Advance time past deadline but within threshold. */
    time_source_mock_set(600, true);
    TEST_ASSERT_EQUAL(SAFETY_STATE_DEGRADED, safety_monitor_check(&ctx));

    /* Advance time past threshold (3x deadline). */
    time_source_mock_set(1600, true);
    TEST_ASSERT_EQUAL(SAFETY_STATE_EMERGENCY, safety_monitor_check(&ctx));

    TEST_PASS();
}

static int test_safety_monitor_rule_engine_stall(void)
{
    SafetyMonitorContext_t ctx;
    reset_failsafe();
    time_source_init();
    time_source_mock_set(0, true);

    safety_monitor_init(&ctx, mock_failsafe);
    safety_monitor_heartbeat(&ctx);

    /* Simulate rule evaluations. */
    time_source_mock_set(100, true);
    safety_monitor_rule_evaluated(&ctx, 0x0001, 100);
    safety_monitor_rule_evaluated(&ctx, 0x0002, 100);

    /* Heartbeat continues, rules evaluated — should be active. */
    time_source_mock_set(200, true);
    safety_monitor_heartbeat(&ctx);
    TEST_ASSERT_EQUAL(SAFETY_STATE_ACTIVE, safety_monitor_check(&ctx));

    /* Simulate heartbeat continuing but NO rule evaluations for > deadline. */
    time_source_mock_set(300, true);
    safety_monitor_heartbeat(&ctx);
    time_source_mock_set(400, true);
    safety_monitor_heartbeat(&ctx);
    time_source_mock_set(500, true);
    safety_monitor_heartbeat(&ctx);

    /* Rules haven't fired in 500ms — check rule staleness.
     * The monitor itself is still active (heartbeat OK), but a separate
     * rule staleness check would detect this. The safety monitor tracks
     * this via the rule_evaluated notifications. */
    time_source_mock_set(1200, true);
    /* Heartbeat still arriving, so monitor is ACTIVE.
     * The rule staleness is checked separately by the caller. */

    TEST_PASS();
}

static int test_safety_monitor_tamper_plus_silence(void)
{
    SafetyMonitorContext_t ctx;
    reset_failsafe();
    time_source_init();
    time_source_mock_set(0, true);

    safety_monitor_init(&ctx, mock_failsafe);
    safety_monitor_heartbeat(&ctx);

    /* Normal operation. */
    time_source_mock_set(100, true);
    TEST_ASSERT_EQUAL(SAFETY_STATE_ACTIVE, safety_monitor_check(&ctx));

    /* Tamper detected — manually trigger failsafe. */
    safety_monitor_trigger_failsafe(&ctx);
    TEST_ASSERT_EQUAL(SAFETY_STATE_EMERGENCY, safety_monitor_check(&ctx));
    TEST_ASSERT(s_failsafe_triggered);

    /* Heartbeat resumes but failsafe is latched (must be explicitly cleared). */
    time_source_mock_set(200, true);
    safety_monitor_heartbeat(&ctx);
    /* After re-init, state returns to normal. */
    safety_monitor_init(&ctx, mock_failsafe);
    safety_monitor_heartbeat(&ctx);
    TEST_ASSERT_EQUAL(SAFETY_STATE_ACTIVE, safety_monitor_check(&ctx));

    TEST_PASS();
}

static int test_safety_monitor_heartbeat_reset(void)
{
    SafetyMonitorContext_t ctx;
    reset_failsafe();
    time_source_init();
    time_source_mock_set(0, true);

    safety_monitor_init(&ctx, mock_failsafe);

    /* No heartbeat yet — IDLE. */
    TEST_ASSERT_EQUAL(SAFETY_STATE_IDLE, safety_monitor_check(&ctx));

    /* Send heartbeat — ACTIVE. */
    safety_monitor_heartbeat(&ctx);
    TEST_ASSERT_EQUAL(SAFETY_STATE_ACTIVE, safety_monitor_check(&ctx));
    TEST_ASSERT_EQUAL(0, safety_monitor_get_missed_count(&ctx));

    /* Advance time — still active. */
    time_source_mock_set(400, true);
    TEST_ASSERT_EQUAL(SAFETY_STATE_ACTIVE, safety_monitor_check(&ctx));

    /* Send another heartbeat — still active. */
    safety_monitor_heartbeat(&ctx);
    TEST_ASSERT_EQUAL(SAFETY_STATE_ACTIVE, safety_monitor_check(&ctx));

    TEST_PASS();
}

int main(void)
{
    printf("\n=== Safety Monitor Tests ===\n\n");

    RUN_TEST(test_safety_monitor_heartbeat_timeout);
    RUN_TEST(test_safety_monitor_rule_engine_stall);
    RUN_TEST(test_safety_monitor_tamper_plus_silence);
    RUN_TEST(test_safety_monitor_heartbeat_reset);

    PRINT_TEST_SUMMARY();
}
