/**
 * @file test_net_resilience.c
 * @brief Tests for network resilience (Phase 24).
 */

#include "test_helpers/test_utils.h"
#include "net_resilience.h"
#include <string.h>

int _total = 0, _passed = 0, _failed = 0;

static int test_offline_queue_overflow_drops_oldest(void)
{
    NetResilienceContext_t ctx;
    net_resilience_init(&ctx);

    /* Fill queue to capacity. */
    OfflineRecord_t rec;
    memset(&rec, 0, sizeof(rec));
    for (uint16_t i = 0; i < OFFLINE_QUEUE_MAX_RECORDS; i++) {
        rec.timestamp_ms = i * 1000;
        rec.node_id = (uint8_t)i;
        TEST_ASSERT(net_resilience_enqueue(&ctx, &rec));
    }
    TEST_ASSERT_EQUAL(OFFLINE_QUEUE_MAX_RECORDS, net_resilience_queue_count(&ctx));

    /* Next enqueue should fail (overflow). */
    rec.timestamp_ms = 999999;
    TEST_ASSERT(!net_resilience_enqueue(&ctx, &rec));
    TEST_ASSERT(net_resilience_queue_overflowed(&ctx));

    /* Dequeue should still return oldest (FIFO). */
    OfflineRecord_t out;
    TEST_ASSERT(net_resilience_dequeue(&ctx, &out));
    TEST_ASSERT_EQUAL(0, out.timestamp_ms);  /* First enqueued. */

    TEST_PASS();
}

static int test_offline_queue_flush_on_reconnect(void)
{
    NetResilienceContext_t ctx;
    net_resilience_init(&ctx);

    /* Enqueue some records while "offline". */
    OfflineRecord_t rec;
    memset(&rec, 0, sizeof(rec));
    for (int i = 0; i < 5; i++) {
        rec.timestamp_ms = (uint32_t)i * 1000;
        rec.node_id = (uint8_t)i;
        net_resilience_enqueue(&ctx, &rec);
    }
    TEST_ASSERT_EQUAL(5, net_resilience_queue_count(&ctx));

    /* Simulate reconnect — flush queue by dequeuing all. */
    uint16_t flushed = 0;
    OfflineRecord_t out;
    while (net_resilience_dequeue(&ctx, &out)) {
        flushed++;
        TEST_ASSERT_EQUAL(flushed - 1, out.node_id);
    }
    TEST_ASSERT_EQUAL(5, flushed);
    TEST_ASSERT_EQUAL(0, net_resilience_queue_count(&ctx));

    TEST_PASS();
}

static int test_backpressure_reduces_publish_rate(void)
{
    NetResilienceContext_t ctx;
    net_resilience_init(&ctx);
    ctx.cloud_publish_rate_ms = 1000; /* 1 second normal rate. */

    /* Normal — can publish immediately. */
    TEST_ASSERT(net_resilience_can_publish(&ctx, 10000));

    /* Moderate pressure — rate reduced. */
    net_resilience_update_connection(&ctx, true, 50000); /* Low memory */
    TEST_ASSERT_EQUAL(PRESSURE_MODERATE, net_resilience_get_pressure(&ctx));

    /* Can't publish immediately after last publish. */
    net_resilience_report_publish(&ctx, 10000);
    TEST_ASSERT(!net_resilience_can_publish(&ctx, 10500)); /* 500ms < 4000ms */

    /* Can publish after extended wait. */
    TEST_ASSERT(net_resilience_can_publish(&ctx, 15000)); /* 5000ms > 4000ms */

    TEST_PASS();
}

static int test_backpressure_critical_drops_non_safety(void)
{
    NetResilienceContext_t ctx;
    net_resilience_init(&ctx);

    /* Critical pressure (very low memory). */
    net_resilience_update_connection(&ctx, true, 5000);
    TEST_ASSERT_EQUAL(PRESSURE_CRITICAL, net_resilience_get_pressure(&ctx));

    /* No publishing allowed. */
    TEST_ASSERT(!net_resilience_can_publish(&ctx, 10000));

    /* Even cloud connected, critical blocks everything. */
    TEST_ASSERT(!net_resilience_can_publish(&ctx, 999999));

    TEST_PASS();
}

static int test_reconnect_exponential_backoff(void)
{
    NetResilienceContext_t ctx;
    net_resilience_init(&ctx);

    /* Initial delay. */
    uint32_t delay = net_resilience_get_reconnect_delay(&ctx);
    TEST_ASSERT_EQUAL(RECONNECT_BASE_DELAY_MS, delay);

    /* First failure — delay doubles. */
    net_resilience_report_reconnect_fail(&ctx);
    delay = net_resilience_get_reconnect_delay(&ctx);
    TEST_ASSERT_EQUAL(RECONNECT_BASE_DELAY_MS * 2, delay);

    /* Second failure — delay doubles again. */
    net_resilience_report_reconnect_fail(&ctx);
    delay = net_resilience_get_reconnect_delay(&ctx);
    TEST_ASSERT_EQUAL(RECONNECT_BASE_DELAY_MS * 4, delay);

    /* Success — reset to base. */
    net_resilience_report_reconnect_success(&ctx);
    delay = net_resilience_get_reconnect_delay(&ctx);
    TEST_ASSERT_EQUAL(RECONNECT_BASE_DELAY_MS, delay);

    TEST_PASS();
}

int main(void)
{
    printf("\n=== Network Resilience Tests ===\n\n");

    RUN_TEST(test_offline_queue_overflow_drops_oldest);
    RUN_TEST(test_offline_queue_flush_on_reconnect);
    RUN_TEST(test_backpressure_reduces_publish_rate);
    RUN_TEST(test_backpressure_critical_drops_non_safety);
    RUN_TEST(test_reconnect_exponential_backoff);

    PRINT_TEST_SUMMARY();
}
