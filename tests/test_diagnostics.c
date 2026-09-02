/**
 * @file test_diagnostics.c
 * @brief Tests for diagnostics (Phase 22).
 */

#include "test_helpers/test_utils.h"
#include "diagnostics.h"
#include <string.h>

int _total = 0, _passed = 0, _failed = 0;

static int test_diagnostics_count_accuracy(void)
{
    DiagnosticsContext_t ctx;
    diagnostics_init(&ctx);

    /* Increment counters. */
    diagnostics_increment(&ctx, "radio_tx_count");
    diagnostics_increment(&ctx, "radio_tx_count");
    diagnostics_increment(&ctx, "radio_tx_count");
    diagnostics_increment(&ctx, "radio_rx_count");
    diagnostics_add(&ctx, "attestation_success", 10);

    /* Verify counts. */
    uint32_t val;
    TEST_ASSERT_EQUAL(DIAG_OP_OK, diagnostics_get_counter(&ctx, "radio_tx_count", &val));
    TEST_ASSERT_EQUAL(3, val);

    TEST_ASSERT_EQUAL(DIAG_OP_OK, diagnostics_get_counter(&ctx, "radio_rx_count", &val));
    TEST_ASSERT_EQUAL(1, val);

    TEST_ASSERT_EQUAL(DIAG_OP_OK, diagnostics_get_counter(&ctx, "attestation_success", &val));
    TEST_ASSERT_EQUAL(10, val);

    TEST_PASS();
}

static int test_diagnostics_json_output(void)
{
    DiagnosticsContext_t ctx;
    diagnostics_init(&ctx);

    diagnostics_increment(&ctx, "radio_tx_count");
    diagnostics_add(&ctx, "historian_writes", 42);

    char json[DIAG_JSON_MAX_SIZE];
    size_t len = diagnostics_to_json(&ctx, json, sizeof(json));

    TEST_ASSERT(len > 0);
    TEST_ASSERT(len < sizeof(json));

    /* Verify it starts with { and ends with }. */
    TEST_ASSERT_EQUAL('{', json[0]);
    TEST_ASSERT_EQUAL('}', json[len - 1]);

    /* Verify it contains our values. */
    TEST_ASSERT(strstr(json, "\"radio_tx\":1") != NULL);
    TEST_ASSERT(strstr(json, "\"historian_writes\":42") != NULL);

    TEST_PASS();
}

static int test_diagnostics_history_ring_buffer(void)
{
    DiagnosticsContext_t ctx;
    diagnostics_init(&ctx);

    /* Write more events than the ring buffer can hold. */
    for (uint16_t i = 0; i < DIAG_HISTORY_SIZE + 10; i++) {
        diagnostics_add(&ctx, "radio_tx_count", 1);
    }

    /* History should be capped at DIAG_HISTORY_SIZE. */
    TEST_ASSERT(ctx.history_count <= DIAG_HISTORY_SIZE);

    /* The most recent events should be present. */
    uint32_t val;
    diagnostics_get_counter(&ctx, "radio_tx_count", &val);
    TEST_ASSERT_EQUAL(DIAG_HISTORY_SIZE + 10, val);

    TEST_PASS();
}

static int test_diagnostics_fault_tracking(void)
{
    DiagnosticsContext_t ctx;
    diagnostics_init(&ctx);

    /* Simulate a fault occurring. */
    ctx.snapshot.active_fault_count = 2;
    ctx.snapshot.current_degradation = DEGRADE_LEVEL_LOCAL_ONLY;

    SystemDiagnostics_t snap;
    diagnostics_get_snapshot(&ctx, &snap);

    TEST_ASSERT_EQUAL(2, snap.active_fault_count);
    TEST_ASSERT_EQUAL(DEGRADE_LEVEL_LOCAL_ONLY, snap.current_degradation);

    /* Clear faults. */
    ctx.snapshot.active_fault_count = 0;
    ctx.snapshot.current_degradation = DEGRADE_LEVEL_NORMAL;

    diagnostics_get_snapshot(&ctx, &snap);
    TEST_ASSERT_EQUAL(0, snap.active_fault_count);
    TEST_ASSERT_EQUAL(DEGRADE_LEVEL_NORMAL, snap.current_degradation);

    TEST_PASS();
}

int main(void)
{
    printf("\n=== Diagnostics Tests ===\n\n");

    RUN_TEST(test_diagnostics_count_accuracy);
    RUN_TEST(test_diagnostics_json_output);
    RUN_TEST(test_diagnostics_history_ring_buffer);
    RUN_TEST(test_diagnostics_fault_tracking);

    PRINT_TEST_SUMMARY();
}
