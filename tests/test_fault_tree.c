/**
 * @file test_fault_tree.c
 * @brief Tests for fault tree (Phase 20).
 */

#include "test_helpers/test_utils.h"
#include "fault_tree.h"

int _total = 0, _passed = 0, _failed = 0;

static int test_fault_tree_radio_lost_degrades_to_local_only(void)
{
    FaultTreeContext_t ctx;
    fault_tree_init(&ctx);

    /* Start normal. */
    TEST_ASSERT_EQUAL(DEGRADE_LEVEL_NORMAL, fault_tree_get_level(&ctx));

    /* Radio lost → L1 (reduced). */
    DegradationLevel_t level = fault_tree_report_fault(&ctx, FAULT_RADIO_LOST, 1000);
    TEST_ASSERT_EQUAL(DEGRADE_LEVEL_REDUCED, level);
    TEST_ASSERT(fault_tree_is_fault_active(&ctx, FAULT_RADIO_LOST));

    /* Cloud also unreachable → L2 (local only). */
    level = fault_tree_report_fault(&ctx, FAULT_CLOUD_UNREACHABLE, 2000);
    TEST_ASSERT_EQUAL(DEGRADE_LEVEL_LOCAL_ONLY, level);

    /* Clear radio — still L2 because cloud is down. */
    level = fault_tree_clear_fault(&ctx, FAULT_RADIO_LOST);
    TEST_ASSERT_EQUAL(DEGRADE_LEVEL_LOCAL_ONLY, level);

    /* Clear cloud — back to normal. */
    level = fault_tree_clear_fault(&ctx, FAULT_CLOUD_UNREACHABLE);
    TEST_ASSERT_EQUAL(DEGRADE_LEVEL_NORMAL, level);

    TEST_PASS();
}

static int test_fault_tree_rule_engine_stall_triggers_emergency(void)
{
    FaultTreeContext_t ctx;
    fault_tree_init(&ctx);

    /* Rule engine stall → L3 (safe mode) immediately. */
    DegradationLevel_t level = fault_tree_report_fault(&ctx, FAULT_RULE_ENGINE_STALL, 1000);
    TEST_ASSERT_EQUAL(DEGRADE_LEVEL_SAFE_MODE, level);

    /* Second stall report → L4 (emergency). */
    level = fault_tree_report_fault(&ctx, FAULT_RULE_ENGINE_STALL, 2000);
    TEST_ASSERT_EQUAL(DEGRADE_LEVEL_EMERGENCY, level);

    /* Clear — back to normal. */
    level = fault_tree_clear_fault(&ctx, FAULT_RULE_ENGINE_STALL);
    TEST_ASSERT_EQUAL(DEGRADE_LEVEL_NORMAL, level);

    TEST_PASS();
}

static int test_fault_tree_recovery_clears_degradation(void)
{
    FaultTreeContext_t ctx;
    fault_tree_init(&ctx);

    /* Multiple faults. */
    fault_tree_report_fault(&ctx, FAULT_RADIO_LOST, 1000);
    fault_tree_report_fault(&ctx, FAULT_SENSOR_INVALID, 2000);
    fault_tree_report_fault(&ctx, FAULT_MEMORY_LOW, 3000);

    /* System level should be the worst of all active faults. */
    DegradationLevel_t level = fault_tree_get_level(&ctx);
    TEST_ASSERT(level >= DEGRADE_LEVEL_REDUCED);

    /* Clear all faults one by one. */
    fault_tree_clear_fault(&ctx, FAULT_RADIO_LOST);
    fault_tree_clear_fault(&ctx, FAULT_SENSOR_INVALID);
    level = fault_tree_clear_fault(&ctx, FAULT_MEMORY_LOW);

    /* Back to normal. */
    TEST_ASSERT_EQUAL(DEGRADE_LEVEL_NORMAL, level);
    TEST_ASSERT_EQUAL(0, fault_tree_get_active_count(&ctx));

    TEST_PASS();
}

static int test_fault_tree_multiple_concurrent_faults(void)
{
    FaultTreeContext_t ctx;
    fault_tree_init(&ctx);

    /* Attestation failed (L2) + memory low (L1). */
    fault_tree_report_fault(&ctx, FAULT_ATTESTATION_FAILED, 1000);
    fault_tree_report_fault(&ctx, FAULT_MEMORY_LOW, 2000);

    /* System level should be the worst: L3 (attestation=L2→L3 after escalation). */
    DegradationLevel_t level = fault_tree_get_level(&ctx);
    TEST_ASSERT(level >= DEGRADE_LEVEL_LOCAL_ONLY);

    /* Active count should be 2. */
    TEST_ASSERT_EQUAL(2, fault_tree_get_active_count(&ctx));

    /* Clear attestation — memory low remains. */
    level = fault_tree_clear_fault(&ctx, FAULT_ATTESTATION_FAILED);
    TEST_ASSERT(level >= DEGRADE_LEVEL_REDUCED);

    /* Clear memory — back to normal. */
    level = fault_tree_clear_fault(&ctx, FAULT_MEMORY_LOW);
    TEST_ASSERT_EQUAL(DEGRADE_LEVEL_NORMAL, level);

    TEST_PASS();
}

int main(void)
{
    printf("\n=== Fault Tree Tests ===\n\n");

    RUN_TEST(test_fault_tree_radio_lost_degrades_to_local_only);
    RUN_TEST(test_fault_tree_rule_engine_stall_triggers_emergency);
    RUN_TEST(test_fault_tree_recovery_clears_degradation);
    RUN_TEST(test_fault_tree_multiple_concurrent_faults);

    PRINT_TEST_SUMMARY();
}
