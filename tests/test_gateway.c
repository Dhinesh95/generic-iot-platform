/**
 * @file test_gateway.c
 * @brief Tests for Gateway fog-computing modules (Phase 13 + 13.1):
 *   - stateful_cache: init, read/write, dirty tracking, capacity
 *   - delta_filter: threshold comparison, first-read, suppress/forward
 *   - batch_forwarder: pack dirty entries, send, clear-on-success, retry-on-fail
 *   - gateway_auth: attestation, HMAC signing/verification, sequence gap detection
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../firmware/gateway/stateful_cache.h"
#include "../firmware/gateway/delta_filter.h"
#include "../firmware/gateway/batch_forwarder.h"
#include "../firmware/gateway/gateway_auth.h"
#include "../firmware/shared/attestation.h"
#include "../firmware/shared/audit_log.h"

/* ---------- Test framework (same as all other test files) ---------- */
static int _tests_run = 0, _tests_passed = 0, _tests_failed = 0;
#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        _tests_failed++; \
        return 1; \
    } \
} while(0)
#define TEST_PASS() do { _tests_passed++; printf("  PASS\n"); return 0; } while(0)
#define RUN_TEST(fn) do { _tests_run++; printf("  Running %s...", #fn); fn(); } while(0)
#define PRINT_TEST_SUMMARY() \
    printf("\n========================================\n"); \
    printf("Tests: %d total, %d passed, %d failed\n", _tests_run, _tests_passed, _tests_failed); \
    printf("========================================\n")

/* ================================================================
 * STATEFUL CACHE TESTS
 * ================================================================ */

static int test_cache_init(void)
{
    cache_init();
    CacheStats_t stats = cache_get_stats();
    TEST_ASSERT(stats.total_updates == 0);
    TEST_ASSERT(cache_entry_count() == 0);
    TEST_PASS();
}

static int test_cache_write_and_read(void)
{
    cache_init();
    TEST_ASSERT(cache_write(0x01, 0, 23.5f) == true);
    float val;
    TEST_ASSERT(cache_read(0x01, 0, &val) == true);
    TEST_ASSERT(val == 23.5f);
    TEST_PASS();
}

static int test_cache_read_miss(void)
{
    cache_init();
    float val;
    TEST_ASSERT(cache_read(0x01, 0, &val) == false);
    TEST_PASS();
}

static int test_cache_write_updates_value(void)
{
    cache_init();
    cache_write(0x01, 0, 10.0f);
    cache_write(0x01, 0, 20.0f);
    float val;
    cache_read(0x01, 0, &val);
    TEST_ASSERT(val == 20.0f);
    TEST_PASS();
}

static int test_cache_dirty_on_change(void)
{
    cache_init();
    cache_write(0x01, 0, 10.0f);
    TEST_ASSERT(cache_is_dirty(0x01, 0) == true);
    cache_write(0x01, 0, 10.0f);  /* Same value — still dirty from first write. */
    TEST_ASSERT(cache_is_dirty(0x01, 0) == true);
    cache_write(0x01, 0, 15.0f);  /* Different value. */
    TEST_ASSERT(cache_is_dirty(0x01, 0) == true);
    TEST_PASS();
}

static int test_cache_clear_dirty(void)
{
    cache_init();
    cache_write(0x01, 0, 10.0f);
    TEST_ASSERT(cache_is_dirty(0x01, 0) == true);
    TEST_ASSERT(cache_clear_dirty(0x01, 0) == true);
    TEST_ASSERT(cache_is_dirty(0x01, 0) == false);
    cache_write(0x01, 0, 20.0f);
    TEST_ASSERT(cache_is_dirty(0x01, 0) == true);
    TEST_PASS();
}

static int test_cache_get_dirty(void)
{
    cache_init();
    cache_write(0x01, 0, 10.0f);
    cache_write(0x01, 1, 20.0f);
    cache_write(0x02, 0, 30.0f);
    CacheEntry_t dirty[8];
    uint8_t count = cache_get_dirty(dirty, 8);
    TEST_ASSERT(count == 3);
    cache_clear_dirty(0x01, 0);
    count = cache_get_dirty(dirty, 8);
    TEST_ASSERT(count == 2);
    TEST_PASS();
}

static int test_cache_multiple_nodes_metrics(void)
{
    cache_init();
    for (uint16_t n = 1; n <= 4; n++) {
        for (uint8_t m = 0; m < 4; m++) {
            TEST_ASSERT(cache_write(n, m, (float)(n * 10 + m)) == true);
        }
    }
    TEST_ASSERT(cache_entry_count() == 16);
    for (uint16_t n = 1; n <= 4; n++) {
        for (uint8_t m = 0; m < 4; m++) {
            float val;
            TEST_ASSERT(cache_read(n, m, &val) == true);
            TEST_ASSERT(val == (float)(n * 10 + m));
        }
    }
    TEST_PASS();
}

static int test_cache_capacity(void)
{
    cache_init();
    uint16_t max_entries = GATEWAY_MAX_NODES * GATEWAY_MAX_METRICS;
    for (uint16_t i = 0; i < max_entries; i++) {
        uint16_t node = (i / GATEWAY_MAX_METRICS) + 1;
        uint8_t  metric = i % GATEWAY_MAX_METRICS;
        TEST_ASSERT(cache_write(node, metric, (float)i) == true);
    }
    uint16_t count = cache_entry_count();
    TEST_ASSERT(count == max_entries);
    TEST_ASSERT(cache_write(100, 0, 999.0f) == false);
    TEST_PASS();
}

static int test_cache_stats(void)
{
    cache_init();
    cache_write(0x01, 0, 10.0f);
    cache_write(0x01, 0, 20.0f);
    cache_write(0x01, 0, 20.0f);
    CacheStats_t stats = cache_get_stats();
    TEST_ASSERT(stats.total_updates == 3);
    TEST_ASSERT(stats.total_hits == 2);
    TEST_ASSERT(stats.total_suppressed == 1);
    TEST_ASSERT(stats.active_entries == 1);
    TEST_PASS();
}

static int test_cache_null_read(void)
{
    cache_init();
    TEST_ASSERT(cache_read(0x01, 0, NULL) == false);
    TEST_PASS();
}

/* ================================================================
 * DELTA FILTER TESTS
 * ================================================================ */

static int test_delta_filter_init(void)
{
    delta_filter_init();
    TEST_ASSERT(delta_filter_config_count() == 0);
    TEST_PASS();
}

static int test_delta_filter_set_threshold(void)
{
    delta_filter_init();
    TEST_ASSERT(delta_filter_set_threshold(0, 1.0f) == true);
    TEST_ASSERT(delta_filter_config_count() == 1);
    TEST_ASSERT(delta_filter_set_threshold(0, 2.0f) == true);
    TEST_ASSERT(delta_filter_config_count() == 1);
    TEST_PASS();
}

static int test_delta_filter_first_read(void)
{
    delta_filter_init();
    cache_init();
    delta_filter_set_threshold(0, 1.0f);
    DeltaFilterResult_t r = delta_filter_evaluate(0x01, 0, 25.0f);
    TEST_ASSERT(r == DELTA_FIRST_READ);
    TEST_PASS();
}

static int test_delta_filter_suppress_below_threshold(void)
{
    delta_filter_init();
    cache_init();
    delta_filter_set_threshold(0, 1.0f);
    cache_write(0x01, 0, 25.0f);
    DeltaFilterResult_t r = delta_filter_evaluate(0x01, 0, 25.0f);
    TEST_ASSERT(r == DELTA_SUPPRESS);
    r = delta_filter_evaluate(0x01, 0, 25.5f);
    TEST_ASSERT(r == DELTA_SUPPRESS);
    r = delta_filter_evaluate(0x01, 0, 26.0f);
    TEST_ASSERT(r == DELTA_SUPPRESS);
    TEST_PASS();
}

static int test_delta_filter_forward_above_threshold(void)
{
    delta_filter_init();
    cache_init();
    delta_filter_set_threshold(0, 1.0f);
    cache_write(0x01, 0, 25.0f);
    DeltaFilterResult_t r = delta_filter_evaluate(0x01, 0, 27.0f);
    TEST_ASSERT(r == DELTA_FORWARD);
    r = delta_filter_evaluate(0x01, 0, 20.0f);
    TEST_ASSERT(r == DELTA_FORWARD);
    TEST_PASS();
}

static int test_delta_filter_no_threshold(void)
{
    delta_filter_init();
    cache_init();
    DeltaFilterResult_t r = delta_filter_evaluate(0x01, 0, 25.0f);
    TEST_ASSERT(r == DELTA_ERR_NOT_FOUND);
    TEST_PASS();
}

static int test_delta_filter_different_metrics_independent(void)
{
    delta_filter_init();
    cache_init();
    delta_filter_set_threshold(0, 1.0f);
    delta_filter_set_threshold(1, 10.0f);
    cache_write(0x01, 0, 100.0f);
    cache_write(0x01, 1, 100.0f);
    DeltaFilterResult_t r0 = delta_filter_evaluate(0x01, 0, 105.0f);
    DeltaFilterResult_t r1 = delta_filter_evaluate(0x01, 1, 105.0f);
    TEST_ASSERT(r0 == DELTA_FORWARD);
    TEST_ASSERT(r1 == DELTA_SUPPRESS);
    TEST_PASS();
}

/* ================================================================
 * BATCH FORWARDER TESTS (Phase 13.1 — v0x02 format)
 * ================================================================ */

static bool s_mock_send_ok = true;
static uint8_t s_mock_send_buf[BATCH_FRAME_MAX_SIZE];
static uint8_t s_mock_send_len = 0;
static uint32_t s_mock_send_count = 0;

static bool mock_radio_send(const uint8_t *data, uint8_t len)
{
    s_mock_send_count++;
    size_t copy_len = len;
    if (copy_len > sizeof(s_mock_send_buf)) copy_len = sizeof(s_mock_send_buf);
    memcpy(s_mock_send_buf, data, copy_len);
    s_mock_send_len = (uint8_t)copy_len;
    return s_mock_send_ok;
}

static void reset_mock(void)
{
    s_mock_send_ok = true;
    s_mock_send_len = 0;
    s_mock_send_count = 0;
}

static int test_batch_forwarder_init(void)
{
    reset_mock();
    batch_forwarder_init(mock_radio_send);
    BatchForwarderStats_t stats = batch_forwarder_get_stats();
    TEST_ASSERT(stats.total_batches == 0);
    TEST_PASS();
}

/* Helper: create an authenticated Gateway auth context for batch forwarder tests.
 * Phase 15.3: batch_forwarder_flush() now requires auth — unsigned frames rejected. */
static GatewayAuthContext_t s_test_gw_auth;
static const uint8_t s_test_fw_key[32] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20
};

static void setup_authenticated_batch_forwarder(void)
{
    batch_forwarder_init(mock_radio_send);
    gateway_auth_init(&s_test_gw_auth, 0xF0, 0x01, s_test_fw_key);
    gateway_set_state(&s_test_gw_auth, GW_AUTH_AUTHENTICATED);
    batch_forwarder_set_auth(&s_test_gw_auth);
}

static int test_batch_forwarder_no_dirty(void)
{
    cache_init();
    reset_mock();
    setup_authenticated_batch_forwarder();
    uint8_t count = batch_forwarder_flush();
    TEST_ASSERT(count == 0);
    TEST_ASSERT(s_mock_send_count == 0);
    TEST_PASS();
}

static int test_batch_forwarder_single_entry(void)
{
    cache_init();
    reset_mock();
    setup_authenticated_batch_forwarder();
    cache_write(0x01, 0, 42.0f);
    uint8_t count = batch_forwarder_flush();
    TEST_ASSERT(count == 1);
    TEST_ASSERT(s_mock_send_count == 1);
    TEST_ASSERT(cache_is_dirty(0x01, 0) == false);

    /* Verify the wire frame (v0x02 format). */
    BatchFrameHeader_t header;
    BatchFrameEntry_t entries[8];
    uint8_t entry_count = 0;
    TEST_ASSERT(batch_decode_frame_untrusted(s_mock_send_buf, s_mock_send_len,
                                   &header, entries, 8, &entry_count) == true);
    TEST_ASSERT(header.magic == 0xB7);
    TEST_ASSERT(header.version == 0x02);
    TEST_ASSERT(entry_count == 1);
    TEST_ASSERT(entries[0].node_id == 0x01);
    TEST_ASSERT(entries[0].metric_id == 0);
    TEST_ASSERT(entries[0].value == 42.0f);
    TEST_PASS();
}

static int test_batch_forwarder_multiple_entries(void)
{
    cache_init();
    reset_mock();
    setup_authenticated_batch_forwarder();
    cache_write(0x01, 0, 10.0f);
    cache_write(0x01, 1, 20.0f);
    cache_write(0x02, 0, 30.0f);
    cache_write(0x02, 1, 40.0f);
    cache_write(0x03, 0, 50.0f);
    uint8_t count = batch_forwarder_flush();
    TEST_ASSERT(count == 5);
    TEST_ASSERT(s_mock_send_count == 1);
    TEST_ASSERT(cache_is_dirty(0x01, 0) == false);
    TEST_ASSERT(cache_is_dirty(0x01, 1) == false);
    TEST_ASSERT(cache_is_dirty(0x02, 0) == false);
    TEST_ASSERT(cache_is_dirty(0x02, 1) == false);
    TEST_ASSERT(cache_is_dirty(0x03, 0) == false);
    TEST_PASS();
}

static int test_batch_forwarder_retry_on_fail(void)
{
    cache_init();
    reset_mock();
    s_mock_send_ok = false;
    setup_authenticated_batch_forwarder();
    cache_write(0x01, 0, 42.0f);
    uint8_t count = batch_forwarder_flush();
    TEST_ASSERT(count == 1);
    TEST_ASSERT(cache_is_dirty(0x01, 0) == true);  /* Not cleared — retry needed. */
    s_mock_send_ok = true;
    count = batch_forwarder_flush();
    TEST_ASSERT(count == 1);
    TEST_ASSERT(cache_is_dirty(0x01, 0) == false);  /* Now cleared. */
    TEST_PASS();
}

static int test_batch_forwarder_stats(void)
{
    cache_init();
    reset_mock();
    setup_authenticated_batch_forwarder();
    cache_write(0x01, 0, 10.0f);
    cache_write(0x02, 0, 20.0f);
    batch_forwarder_flush();
    BatchForwarderStats_t stats = batch_forwarder_get_stats();
    TEST_ASSERT(stats.total_batches == 1);
    TEST_ASSERT(stats.total_entries_sent == 2);
    TEST_ASSERT(stats.total_send_ok == 1);
    TEST_ASSERT(stats.total_send_fail == 0);
    s_mock_send_ok = false;
    cache_write(0x03, 0, 30.0f);
    batch_forwarder_flush();
    stats = batch_forwarder_get_stats();
    TEST_ASSERT(stats.total_batches == 2);
    TEST_ASSERT(stats.total_entries_sent == 2);
    TEST_ASSERT(stats.total_send_ok == 1);
    TEST_ASSERT(stats.total_send_fail == 1);
    TEST_PASS();
}

static int test_batch_encode_decode_roundtrip(void)
{
    BatchFrameHeader_t header = {
        .magic = 0xB7, .version = 0x02, .entry_count = 3,
        .flags = 0, .sequence_number = 42
    };
    BatchFrameEntry_t entries[3] = {
        { .node_id = 0x0102, .metric_id = 5, .value = 3.14f },
        { .node_id = 0xABCD, .metric_id = 0, .value = -1.0f },
        { .node_id = 0x0001, .metric_id = 15, .value = 1000.0f },
    };
    uint8_t buf[128];
    size_t len = 0;
    TEST_ASSERT(batch_encode_frame(&header, entries, 3, buf, sizeof(buf), &len) == true);
    TEST_ASSERT(len == BATCH_FRAME_HEADER_SIZE + 3 * BATCH_FRAME_ENTRY_SIZE + BATCH_FRAME_HMAC_SIZE);
    BatchFrameHeader_t out_header;
    BatchFrameEntry_t out_entries[8];
    uint8_t out_count = 0;
    TEST_ASSERT(batch_decode_frame_untrusted(buf, len, &out_header, out_entries, 8, &out_count) == true);
    TEST_ASSERT(out_count == 3);
    TEST_ASSERT(out_header.sequence_number == 42);
    TEST_ASSERT(out_entries[0].node_id == 0x0102);
    TEST_ASSERT(out_entries[0].value == 3.14f);
    TEST_ASSERT(out_entries[1].node_id == 0xABCD);
    TEST_ASSERT(out_entries[2].value == 1000.0f);
    TEST_PASS();
}

static int test_batch_decode_bad_magic(void)
{
    uint8_t buf[8] = { 0x00, 0x02, 0x00, 0x00, 0, 0, 0, 0 };
    BatchFrameHeader_t header;
    BatchFrameEntry_t entries[8];
    uint8_t count = 0;
    TEST_ASSERT(batch_decode_frame_untrusted(buf, 8, &header, entries, 8, &count) == false);
    TEST_PASS();
}

static int test_batch_decode_truncated(void)
{
    uint8_t buf[2] = { 0xB7, 0x02 };
    BatchFrameHeader_t header;
    BatchFrameEntry_t entries[8];
    uint8_t count = 0;
    TEST_ASSERT(batch_decode_frame_untrusted(buf, 2, &header, entries, 8, &count) == false);
    TEST_PASS();
}

static int test_batch_encode_buffer_too_small(void)
{
    BatchFrameHeader_t header = { .magic = 0xB7, .version = 0x02, .entry_count = 1, .flags = 0, .sequence_number = 0 };
    BatchFrameEntry_t entry = { .node_id = 1, .metric_id = 0, .value = 1.0f };
    uint8_t buf[2];
    size_t len = 0;
    TEST_ASSERT(batch_encode_frame(&header, &entry, 1, buf, sizeof(buf), &len) == false);
    TEST_PASS();
}

static int test_batch_forwarder_no_send_func(void)
{
    cache_init();
    batch_forwarder_init(NULL);
    cache_write(0x01, 0, 10.0f);
    uint8_t count = batch_forwarder_flush();
    TEST_ASSERT(count == 0);
    TEST_PASS();
}

/* ---------- Test: flush rejected when auth not set (Phase 15.3) ---------- */
static int test_batch_forwarder_rejects_no_auth(void)
{
    /*
     * Phase 15.3: batch_forwarder_flush() now requires auth.
     * Without auth, flush returns 0 and entries stay dirty.
     * This prevents accidental sending of unsigned v0x02 frames
     * (which had zeroed HMAC — same fragility class as Phase 13.2).
     */
    cache_init();
    reset_mock();
    batch_forwarder_init(mock_radio_send);
    /* Do NOT set auth — s_auth_ctx remains NULL. */
    cache_write(0x01, 0, 42.0f);
    uint8_t count = batch_forwarder_flush();
    TEST_ASSERT(count == 0);  /* Rejected — no auth. */
    TEST_ASSERT(s_mock_send_count == 0);  /* Nothing sent. */
    TEST_ASSERT(cache_is_dirty(0x01, 0) == true);  /* Entries stay dirty for retry. */
    TEST_PASS();
}

static int test_batch_decode_v01_backward_compat(void)
{
    /* v0x01 format: 4-byte header, no sequence, no HMAC. */
    uint8_t manual_buf[11];
    manual_buf[0] = 0xB7; manual_buf[1] = 0x01; manual_buf[2] = 0x01; manual_buf[3] = 0x00;
    manual_buf[4] = 0x01; manual_buf[5] = 0x00;  /* node_id LE */
    manual_buf[6] = 0x00;  /* metric_id */
    float v = 42.0f;
    memcpy(&manual_buf[7], &v, 4);

    BatchFrameHeader_t header;
    BatchFrameEntry_t entries[8];
    uint8_t count = 0;
    TEST_ASSERT(batch_decode_frame_untrusted(manual_buf, 11, &header, entries, 8, &count) == true);
    TEST_ASSERT(header.version == 0x01);
    TEST_ASSERT(header.sequence_number == 0);
    TEST_ASSERT(count == 1);
    TEST_ASSERT(entries[0].node_id == 0x0001);
    TEST_ASSERT(entries[0].value == 42.0f);
    TEST_PASS();
}

/* ================================================================
 * GATEWAY AUTH TESTS (Phase 13.1)
 * ================================================================ */

/* Test session key (pre-shared between Gateway and Hub). */
static const uint8_t test_session_key[32] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20
};

static int test_gateway_auth_init(void)
{
    GatewayAuthContext_t ctx;
    gateway_auth_init(&ctx, 0xF0, 0x01, test_session_key);
    TEST_ASSERT(ctx.gateway_node_id == 0xF0);
    TEST_ASSERT(ctx.hub_node_id == 0x01);
    TEST_ASSERT(gateway_get_state(&ctx) == GW_AUTH_IDLE);
    TEST_ASSERT(gateway_get_sequence(&ctx) == 0);
    TEST_PASS();
}

static int test_gateway_auth_state_transitions(void)
{
    GatewayAuthContext_t ctx;
    gateway_auth_init(&ctx, 0xF0, 0x01, test_session_key);
    TEST_ASSERT(gateway_get_state(&ctx) == GW_AUTH_IDLE);
    gateway_set_state(&ctx, GW_AUTH_AUTHENTICATED);
    TEST_ASSERT(gateway_get_state(&ctx) == GW_AUTH_AUTHENTICATED);
    TEST_PASS();
}

static int test_gateway_auth_sign_requires_authenticated(void)
{
    GatewayAuthContext_t ctx;
    gateway_auth_init(&ctx, 0xF0, 0x01, test_session_key);
    /* State is IDLE — sign should fail. */
    uint8_t frame[8] = { 0xB7, 0x02, 0x01, 0x00, 0, 0, 0, 0 };
    uint8_t hmac[32];
    TEST_ASSERT(gateway_sign_frame(&ctx, frame, sizeof(frame), hmac) == false);
    TEST_PASS();
}

static int test_gateway_auth_sign_and_verify(void)
{
    /* Gateway side. */
    GatewayAuthContext_t gw_ctx;
    gateway_auth_init(&gw_ctx, 0xF0, 0x01, test_session_key);
    gateway_set_state(&gw_ctx, GW_AUTH_AUTHENTICATED);

    /* Build a sample frame. */
    uint8_t frame[8 + 7] = { 0 };
    frame[0] = 0xB7; frame[1] = 0x02; frame[2] = 0x01; frame[3] = 0x00;
    /* sequence = 1 */
    frame[4] = 0x01; frame[5] = 0x00; frame[6] = 0x00; frame[7] = 0x00;
    /* entry: node=0x0001, metric=0, value=42.0 */
    frame[8] = 0x01; frame[9] = 0x00; frame[10] = 0x00;
    float v = 42.0f;
    memcpy(&frame[11], &v, 4);

    /* Sign. */
    uint8_t hmac[32];
    TEST_ASSERT(gateway_sign_frame(&gw_ctx, frame, sizeof(frame), hmac) == true);

    /* Append HMAC to frame for verification. */
    uint8_t wire_frame[8 + 7 + 32];
    memcpy(wire_frame, frame, sizeof(frame));
    memcpy(wire_frame + sizeof(frame), hmac, 32);

    /* Hub side. */
    GatewayGatewayTracker_t tracker;
    gateway_tracker_init(&tracker, 0xF0, test_session_key);
    tracker.authenticated = true;

    GatewayFrameResult_t r = gateway_verify_frame(&tracker, wire_frame, sizeof(wire_frame));
    TEST_ASSERT(r == GW_FRAME_OK);
    TEST_PASS();
}

static int test_gateway_auth_tampered_frame_detected(void)
{
    /* Gateway side — sign a frame. */
    GatewayAuthContext_t gw_ctx;
    gateway_auth_init(&gw_ctx, 0xF0, 0x01, test_session_key);
    gateway_set_state(&gw_ctx, GW_AUTH_AUTHENTICATED);

    uint8_t frame[8 + 7] = { 0 };
    frame[0] = 0xB7; frame[1] = 0x02; frame[2] = 0x01; frame[3] = 0x00;
    frame[4] = 0x01; frame[5] = 0x00; frame[6] = 0x00; frame[7] = 0x00;
    frame[8] = 0x01; frame[9] = 0x00; frame[10] = 0x00;
    float v = 42.0f;
    memcpy(&frame[11], &v, 4);

    uint8_t hmac[32];
    gateway_sign_frame(&gw_ctx, frame, sizeof(frame), hmac);

    uint8_t wire_frame[8 + 7 + 32];
    memcpy(wire_frame, frame, sizeof(frame));
    memcpy(wire_frame + sizeof(frame), hmac, 32);

    /* Tamper: change the value in the entry. */
    float bad_val = 99.0f;
    memcpy(&wire_frame[11], &bad_val, 4);

    /* Hub side — should detect tamper. */
    GatewayGatewayTracker_t tracker;
    gateway_tracker_init(&tracker, 0xF0, test_session_key);
    tracker.authenticated = true;

    GatewayFrameResult_t r = gateway_verify_frame(&tracker, wire_frame, sizeof(wire_frame));
    TEST_ASSERT(r == GW_FRAME_ERR_HMAC_MISMATCH);
    TEST_PASS();
}

static int test_gateway_auth_tampered_count_detected(void)
{
    /* Sign with entry_count=1 but tamper the count to 2. */
    GatewayAuthContext_t gw_ctx;
    gateway_auth_init(&gw_ctx, 0xF0, 0x01, test_session_key);
    gateway_set_state(&gw_ctx, GW_AUTH_AUTHENTICATED);

    uint8_t frame[8 + 7] = { 0 };
    frame[0] = 0xB7; frame[1] = 0x02; frame[2] = 0x01; frame[3] = 0x00;
    frame[4] = 0x01; frame[5] = 0x00; frame[6] = 0x00; frame[7] = 0x00;
    frame[8] = 0x01; frame[9] = 0x00; frame[10] = 0x00;
    float v = 42.0f;
    memcpy(&frame[11], &v, 4);

    uint8_t hmac[32];
    gateway_sign_frame(&gw_ctx, frame, sizeof(frame), hmac);

    uint8_t wire_frame[8 + 7 + 32];
    memcpy(wire_frame, frame, sizeof(frame));
    memcpy(wire_frame + sizeof(frame), hmac, 32);

    /* Tamper: change entry_count from 1 to 2. */
    wire_frame[2] = 0x02;

    GatewayGatewayTracker_t tracker;
    gateway_tracker_init(&tracker, 0xF0, test_session_key);
    tracker.authenticated = true;

    GatewayFrameResult_t r = gateway_verify_frame(&tracker, wire_frame, sizeof(wire_frame));
    TEST_ASSERT(r == GW_FRAME_ERR_HMAC_MISMATCH);
    TEST_PASS();
}

static int test_gateway_auth_not_authenticated_rejected(void)
{
    GatewayGatewayTracker_t tracker;
    gateway_tracker_init(&tracker, 0xF0, test_session_key);
    tracker.authenticated = false;  /* Not yet attested. */

    uint8_t frame[8 + 32] = { 0 };
    GatewayFrameResult_t r = gateway_verify_frame(&tracker, frame, sizeof(frame));
    TEST_ASSERT(r == GW_FRAME_ERR_NOT_AUTH);
    TEST_PASS();
}

static int test_gateway_auth_sequence_advances(void)
{
    GatewayAuthContext_t ctx;
    gateway_auth_init(&ctx, 0xF0, 0x01, test_session_key);
    TEST_ASSERT(gateway_get_sequence(&ctx) == 0);
    gateway_advance_sequence(&ctx);
    TEST_ASSERT(gateway_get_sequence(&ctx) == 1);
    gateway_advance_sequence(&ctx);
    TEST_ASSERT(gateway_get_sequence(&ctx) == 2);
    TEST_PASS();
}

static int test_gateway_auth_wrong_key_detected(void)
{
    /* Sign with one key, verify with a different key. */
    GatewayAuthContext_t gw_ctx;
    uint8_t gw_key[32] = { 0xAA };
    gateway_auth_init(&gw_ctx, 0xF0, 0x01, gw_key);
    gateway_set_state(&gw_ctx, GW_AUTH_AUTHENTICATED);

    uint8_t frame[8] = { 0xB7, 0x02, 0x01, 0x00, 0, 0, 0, 0 };
    uint8_t hmac[32];
    gateway_sign_frame(&gw_ctx, frame, sizeof(frame), hmac);

    uint8_t wire_frame[8 + 32];
    memcpy(wire_frame, frame, sizeof(frame));
    memcpy(wire_frame + sizeof(frame), hmac, 32);

    /* Verify with wrong key. */
    GatewayGatewayTracker_t tracker;
    uint8_t hub_key[32] = { 0xBB };  /* Different key! */
    gateway_tracker_init(&tracker, 0xF0, hub_key);
    tracker.authenticated = true;

    GatewayFrameResult_t r = gateway_verify_frame(&tracker, wire_frame, sizeof(wire_frame));
    TEST_ASSERT(r == GW_FRAME_ERR_HMAC_MISMATCH);
    TEST_PASS();
}

/* ---------- Test: v0x01 downgrade rejected when auth required (Phase 13.2) ---------- */
static int test_gateway_v0x01_rejected_when_auth_required(void)
{
    /*
     * CRITICAL SECURITY TEST: proves that a v0x01 frame (no HMAC, no
     * sequence number) is rejected by gateway_verify_frame when the Hub
     * has authentication enabled. This prevents a downgrade attack where
     * an attacker crafts a v0x01 frame with arbitrary entries and the
     * Hub processes them as trusted data.
     *
     * Before Phase 13.2 fix: the frame was rejected accidentally (size
     * check required >= 40 bytes, v0x01 frames are smaller). After fix:
     * rejected explicitly by version check with audit-log entry.
     */
    GatewayGatewayTracker_t tracker;
    gateway_tracker_init(&tracker, 0xF0, test_session_key);
    tracker.authenticated = true;  /* Auth is active. */

    /* Craft a valid-structure v0x01 frame: 4-byte header + 1 entry (7 bytes) = 11 bytes.
     * No HMAC tag, no sequence number — the old format. */
    uint8_t v01_frame[4 + 7];
    v01_frame[0] = 0xB7;  /* magic */
    v01_frame[1] = 0x01;  /* version = 0x01 (old format) */
    v01_frame[2] = 0x01;  /* entry_count = 1 */
    v01_frame[3] = 0x00;  /* flags */
    /* entry: node=0x0001, metric=0, value=42.0 */
    v01_frame[4] = 0x01; v01_frame[5] = 0x00; v01_frame[6] = 0x00;
    float v = 42.0f;
    memcpy(&v01_frame[7], &v, 4);

    /* Verify: MUST be rejected, not silently accepted. */
    GatewayFrameResult_t r = gateway_verify_frame(&tracker, v01_frame, sizeof(v01_frame));
    TEST_ASSERT(r == GW_FRAME_ERR_HMAC_MISMATCH);  /* Rejected — version check triggers. */
    TEST_PASS();
}

/* ---------- Test: v0x01 accepted by decode but NOT by verify (Phase 13.2) ---------- */
static int test_gateway_v0x01_decode_vs_verify(void)
{
    /*
     * Proves the precise distinction: batch_decode_frame_untrusted (byte-layout
     * parser) accepts v0x01 for backward-compat decoding, but
     * gateway_verify_frame (auth gate) rejects it. The caller MUST
     * go through verify before decode — decode alone is not an auth gate.
     */
    /* Build a v0x01 frame. */
    uint8_t v01_frame[4 + 7];
    v01_frame[0] = 0xB7; v01_frame[1] = 0x01; v01_frame[2] = 0x01; v01_frame[3] = 0x00;
    v01_frame[4] = 0x01; v01_frame[5] = 0x00; v01_frame[6] = 0x00;
    float v = 42.0f;
    memcpy(&v01_frame[7], &v, 4);

    /* batch_decode_frame_untrusted: accepts v0x01 (byte-layout backward compat). */
    BatchFrameHeader_t hdr;
    BatchFrameEntry_t entries[8];
    uint8_t count = 0;
    bool decoded = batch_decode_frame_untrusted(v01_frame, sizeof(v01_frame), &hdr, entries, 8, &count);
    TEST_ASSERT(decoded == true);   /* Parser accepts it. */
    TEST_ASSERT(hdr.version == 0x01);
    TEST_ASSERT(count == 1);
    TEST_ASSERT(entries[0].value == 42.0f);

    /* gateway_verify_frame: rejects it (auth gate). */
    GatewayGatewayTracker_t tracker;
    gateway_tracker_init(&tracker, 0xF0, test_session_key);
    tracker.authenticated = true;

    GatewayFrameResult_t r = gateway_verify_frame(&tracker, v01_frame, sizeof(v01_frame));
    TEST_ASSERT(r == GW_FRAME_ERR_HMAC_MISMATCH);  /* Rejected. */

    /* Conclusion: decode alone does NOT grant trust. The caller must
     * verify first, then decode. A v0x01 frame can be parsed but
     * never trusted when auth is active. */
    TEST_PASS();
}

/* ================================================================
 * GATEWAY INGEST FRAME TESTS (Phase 13.3)
 * ================================================================ */

static int test_gateway_ingest_frame_rejects_unverified(void)
{
    /*
     * Proves: gateway_ingest_frame rejects a v0x01 frame (no HMAC)
     * when auth is active, and does NOT produce decoded entries.
     */
    GatewayGatewayTracker_t tracker;
    gateway_tracker_init(&tracker, 0xF0, test_session_key);
    tracker.authenticated = true;

    /* v0x01 frame: valid structure, no HMAC. */
    uint8_t v01_frame[4 + 7];
    v01_frame[0] = 0xB7; v01_frame[1] = 0x01; v01_frame[2] = 0x01; v01_frame[3] = 0x00;
    v01_frame[4] = 0x01; v01_frame[5] = 0x00; v01_frame[6] = 0x00;
    float v = 42.0f;
    memcpy(&v01_frame[7], &v, 4);

    BatchFrameHeader_t hdr;
    BatchFrameEntry_t entries[8];
    uint8_t count = 0xFF;  /* Sentinel: verify it gets set to 0. */

    GatewayFrameResult_t r = gateway_ingest_frame(
        &tracker, EDGE_TRACKER_CHECK_DISABLED_TESTING_ONLY, v01_frame, sizeof(v01_frame),
        &hdr, entries, 8, &count);

    TEST_ASSERT(r == GW_FRAME_ERR_HMAC_MISMATCH);  /* Rejected. */
    TEST_ASSERT(count == 0);  /* No entries decoded — rejection happened before decode. */
    TEST_PASS();
}

static int test_gateway_ingest_frame_accepts_verified(void)
{
    /*
     * Proves: gateway_ingest_frame accepts a valid v0x02 frame
     * (HMAC-signed) and produces correctly decoded entries.
     */
    /* Gateway side: sign a frame. */
    GatewayAuthContext_t gw_ctx;
    gateway_auth_init(&gw_ctx, 0xF0, 0x01, test_session_key);
    gateway_set_state(&gw_ctx, GW_AUTH_AUTHENTICATED);

    /* Encode a frame with 2 entries. */
    BatchFrameHeader_t header = {
        .magic = 0xB7, .version = 0x02, .entry_count = 2,
        .flags = 0, .sequence_number = 0
    };
    BatchFrameEntry_t src_entries[2] = {
        { .node_id = 0x01, .metric_id = 0, .value = 10.0f },
        { .node_id = 0x02, .metric_id = 1, .value = 20.0f }
    };
    uint8_t wire_buf[BATCH_FRAME_MAX_SIZE];
    size_t wire_len = 0;
    batch_encode_frame(&header, src_entries, 2, wire_buf, sizeof(wire_buf), &wire_len);

    /* Sign. */
    uint8_t hmac[32];
    gateway_sign_frame(&gw_ctx, wire_buf, wire_len - GATEWAY_HMAC_SIZE, hmac);
    memcpy(&wire_buf[wire_len - GATEWAY_HMAC_SIZE], hmac, GATEWAY_HMAC_SIZE);

    /* Hub side: ingest. */
    GatewayGatewayTracker_t tracker;
    gateway_tracker_init(&tracker, 0xF0, test_session_key);
    tracker.authenticated = true;

    BatchFrameHeader_t out_hdr;
    BatchFrameEntry_t out_entries[8];
    uint8_t out_count = 0;

    GatewayFrameResult_t r = gateway_ingest_frame(
        &tracker, EDGE_TRACKER_CHECK_DISABLED_TESTING_ONLY, wire_buf, wire_len,
        &out_hdr, out_entries, 8, &out_count);

    TEST_ASSERT(r == GW_FRAME_OK);
    TEST_ASSERT(out_count == 2);
    TEST_ASSERT(out_entries[0].node_id == 0x01);
    TEST_ASSERT(out_entries[0].value == 10.0f);
    TEST_ASSERT(out_entries[1].node_id == 0x02);
    TEST_ASSERT(out_entries[1].value == 20.0f);
    TEST_ASSERT(tracker.last_sequence == 0);  /* Sequence updated. */
    TEST_PASS();
}

static int test_gateway_ingest_frame_identical_output(void)
{
    /*
     * Proves: gateway_ingest_frame produces identical decoded entries
     * to the manual verify-then-decode sequence. No behavior change,
     * just enforced ordering.
     */
    /* Gateway side: sign a frame. */
    GatewayAuthContext_t gw_ctx;
    gateway_auth_init(&gw_ctx, 0xF0, 0x01, test_session_key);
    gateway_set_state(&gw_ctx, GW_AUTH_AUTHENTICATED);

    BatchFrameHeader_t header = {
        .magic = 0xB7, .version = 0x02, .entry_count = 3,
        .flags = 0, .sequence_number = 5
    };
    BatchFrameEntry_t src_entries[3] = {
        { .node_id = 0xAA, .metric_id = 2, .value = 3.14f },
        { .node_id = 0xBB, .metric_id = 5, .value = -1.0f },
        { .node_id = 0xCC, .metric_id = 8, .value = 999.0f }
    };
    uint8_t wire_buf[BATCH_FRAME_MAX_SIZE];
    size_t wire_len = 0;
    batch_encode_frame(&header, src_entries, 3, wire_buf, sizeof(wire_buf), &wire_len);
    uint8_t hmac[32];
    gateway_sign_frame(&gw_ctx, wire_buf, wire_len - GATEWAY_HMAC_SIZE, hmac);
    memcpy(&wire_buf[wire_len - GATEWAY_HMAC_SIZE], hmac, GATEWAY_HMAC_SIZE);

    /* Method A: manual verify-then-decode (the old pattern). */
    GatewayGatewayTracker_t tracker_a;
    gateway_tracker_init(&tracker_a, 0xF0, test_session_key);
    tracker_a.authenticated = true;
    GatewayFrameResult_t va = gateway_verify_frame(&tracker_a, wire_buf, wire_len);
    TEST_ASSERT(va == GW_FRAME_OK);
    BatchFrameHeader_t hdr_a;
    BatchFrameEntry_t entries_a[8];
    uint8_t count_a = 0;
    bool da = batch_decode_frame_untrusted(wire_buf, wire_len, &hdr_a, entries_a, 8, &count_a);
    TEST_ASSERT(da == true);

    /* Method B: single ingest entry point (the new pattern). */
    GatewayGatewayTracker_t tracker_b;
    gateway_tracker_init(&tracker_b, 0xF0, test_session_key);
    tracker_b.authenticated = true;
    BatchFrameHeader_t hdr_b;
    BatchFrameEntry_t entries_b[8];
    uint8_t count_b = 0;
    GatewayFrameResult_t vb = gateway_ingest_frame(
        &tracker_b, EDGE_TRACKER_CHECK_DISABLED_TESTING_ONLY, wire_buf, wire_len,
        &hdr_b, entries_b, 8, &count_b);
    TEST_ASSERT(vb == GW_FRAME_OK);

    /* Results must be identical. */
    TEST_ASSERT(count_a == count_b);
    TEST_ASSERT(hdr_a.entry_count == hdr_b.entry_count);
    TEST_ASSERT(hdr_a.sequence_number == hdr_b.sequence_number);
    for (uint8_t i = 0; i < count_a; i++) {
        TEST_ASSERT(entries_a[i].node_id == entries_b[i].node_id);
        TEST_ASSERT(entries_a[i].metric_id == entries_b[i].metric_id);
        TEST_ASSERT(entries_a[i].value == entries_b[i].value);
    }
    TEST_PASS();
}

/* ---------- Test: NULL edge_tracker rejected (Phase 15.2) ---------- */
static int test_gateway_ingest_frame_null_edge_tracker_is_error(void)
{
    /*
     * CRITICAL SECURITY TEST (Phase 15.2): proves that a NULL edge_tracker
     * is rejected by gateway_ingest_frame — it is NOT a silent "skip trust
     * check" signal. This mirrors Phase 14.2's test for edge_node_init()
     * where NULL auth_ctx is rejected.
     *
     * Before Phase 15.2 fix: NULL meant "skip trust check" (vulnerability).
     * After fix: NULL is a parameter error (fail-closed).
     */
    GatewayGatewayTracker_t tracker;
    gateway_tracker_init(&tracker, 0xF0, test_session_key);
    tracker.authenticated = true;

    /* Build a valid v0x02 frame. */
    GatewayAuthContext_t gw_ctx;
    gateway_auth_init(&gw_ctx, 0xF0, 0x01, test_session_key);
    gateway_set_state(&gw_ctx, GW_AUTH_AUTHENTICATED);

    BatchFrameHeader_t header = {
        .magic = 0xB7, .version = 0x02, .entry_count = 1,
        .flags = 0, .sequence_number = 0
    };
    BatchFrameEntry_t entry = { .node_id = 1, .metric_id = 0, .value = 42.0f };
    uint8_t wire_buf[BATCH_FRAME_MAX_SIZE];
    size_t wire_len = 0;
    batch_encode_frame(&header, &entry, 1, wire_buf, sizeof(wire_buf), &wire_len);
    uint8_t hmac[GATEWAY_HMAC_SIZE];
    gateway_sign_frame(&gw_ctx, wire_buf, wire_len - GATEWAY_HMAC_SIZE, hmac);
    memcpy(&wire_buf[wire_len - GATEWAY_HMAC_SIZE], hmac, GATEWAY_HMAC_SIZE);

    /* NULL edge_tracker MUST be rejected — not silently skipped. */
    BatchFrameHeader_t out_hdr;
    BatchFrameEntry_t out_entries[8];
    uint8_t out_count = 0xFF;  /* Sentinel: verify it gets set to 0. */

    GatewayFrameResult_t r = gateway_ingest_frame(
        &tracker, NULL, wire_buf, wire_len,
        &out_hdr, out_entries, 8, &out_count);

    TEST_ASSERT(r == GW_FRAME_ERR_PARAM_NULL);  /* Rejected. */
    TEST_ASSERT(out_count == 0);  /* No entries decoded — fail-closed. */
    TEST_PASS();
}

/* ---------- Test: explicit disabled sentinel still works (Phase 15.2) ---------- */
static int test_gateway_ingest_frame_explicit_disabled_sentinel_still_works(void)
{
    /*
     * EDGE_TRACKER_CHECK_DISABLED_TESTING_ONLY explicitly disables the
     * trust check for test harness use. This is the ONLY way to bypass
     * the trust check — the sentinel name makes the security bypass
     * visible at the call site.
     */
    GatewayGatewayTracker_t tracker;
    gateway_tracker_init(&tracker, 0xF0, test_session_key);
    tracker.authenticated = true;

    GatewayAuthContext_t gw_ctx;
    gateway_auth_init(&gw_ctx, 0xF0, 0x01, test_session_key);
    gateway_set_state(&gw_ctx, GW_AUTH_AUTHENTICATED);

    BatchFrameHeader_t header = {
        .magic = 0xB7, .version = 0x02, .entry_count = 1,
        .flags = 0, .sequence_number = 0
    };
    BatchFrameEntry_t entry = { .node_id = 1, .metric_id = 0, .value = 42.0f };
    uint8_t wire_buf[BATCH_FRAME_MAX_SIZE];
    size_t wire_len = 0;
    batch_encode_frame(&header, &entry, 1, wire_buf, sizeof(wire_buf), &wire_len);
    uint8_t hmac[GATEWAY_HMAC_SIZE];
    gateway_sign_frame(&gw_ctx, wire_buf, wire_len - GATEWAY_HMAC_SIZE, hmac);
    memcpy(&wire_buf[wire_len - GATEWAY_HMAC_SIZE], hmac, GATEWAY_HMAC_SIZE);

    /* Explicit sentinel: trust check is bypassed, entry passes through. */
    BatchFrameHeader_t out_hdr;
    BatchFrameEntry_t out_entries[8];
    uint8_t out_count = 0;

    GatewayFrameResult_t r = gateway_ingest_frame(
        &tracker, EDGE_TRACKER_CHECK_DISABLED_TESTING_ONLY, wire_buf, wire_len,
        &out_hdr, out_entries, 8, &out_count);

    TEST_ASSERT(r == GW_FRAME_OK);
    TEST_ASSERT(out_count == 1);  /* Entry passes through — trust check skipped. */
    TEST_ASSERT(out_entries[0].node_id == 1);
    TEST_ASSERT(out_entries[0].value == 42.0f);
    TEST_PASS();
}

/* ================================================================
 * INTEGRATION: delta filter + cache + batch forwarder + auth
 * ================================================================ */

static int test_integration_auth_delta_to_batch(void)
{
    cache_init();
    delta_filter_init();
    reset_mock();
    batch_forwarder_init(mock_radio_send);

    /* Set up auth context. */
    GatewayAuthContext_t gw_ctx;
    gateway_auth_init(&gw_ctx, 0xF0, 0x01, test_session_key);
    gateway_set_state(&gw_ctx, GW_AUTH_AUTHENTICATED);
    batch_forwarder_set_auth(&gw_ctx);

    delta_filter_set_threshold(0, 0.5f);

    /* Edge Node 1: first read → forward. */
    DeltaFilterResult_t r = delta_filter_evaluate(0x01, 0, 25.0f);
    TEST_ASSERT(r == DELTA_FIRST_READ);
    cache_write(0x01, 0, 25.0f);

    /* Edge Node 2: first read → forward. */
    r = delta_filter_evaluate(0x02, 0, 100.0f);
    TEST_ASSERT(r == DELTA_FIRST_READ);
    cache_write(0x02, 0, 100.0f);

    /* Node 1: below threshold → suppress. */
    r = delta_filter_evaluate(0x01, 0, 25.3f);
    TEST_ASSERT(r == DELTA_SUPPRESS);

    /* Node 2: above threshold → forward. */
    r = delta_filter_evaluate(0x02, 0, 101.0f);
    TEST_ASSERT(r == DELTA_FORWARD);
    cache_write(0x02, 0, 101.0f);

    /* Batch flush with auth: should produce signed frame. */
    uint8_t count = batch_forwarder_flush();
    TEST_ASSERT(count == 2);

    /* Sequence should have advanced. */
    TEST_ASSERT(gateway_get_sequence(&gw_ctx) == 1);

    /* Verify the frame HMAC (Hub side). */
    GatewayGatewayTracker_t tracker;
    gateway_tracker_init(&tracker, 0xF0, test_session_key);
    tracker.authenticated = true;

    GatewayFrameResult_t ver = gateway_verify_frame(&tracker, s_mock_send_buf, s_mock_send_len);
    TEST_ASSERT(ver == GW_FRAME_OK);

    /* Update sequence on Hub side. */
    BatchFrameHeader_t hdr;
    BatchFrameEntry_t ents[8];
    uint8_t ec = 0;
    batch_decode_frame_untrusted(s_mock_send_buf, s_mock_send_len, &hdr, ents, 8, &ec);
    gateway_tracker_update_sequence(&tracker, hdr.sequence_number);
    TEST_ASSERT(tracker.last_sequence == 0);  /* Frame was encoded with seq=0 (before advance). */
    TEST_PASS();
}

static int test_integration_sequence_gap_detected(void)
{
    GatewayGatewayTracker_t tracker;
    gateway_tracker_init(&tracker, 0xF0, test_session_key);
    tracker.authenticated = true;

    /* Simulate: accept sequence 1, then receive sequence 3 (gap). */
    gateway_tracker_update_sequence(&tracker, 1);

    /* Build a frame with sequence=3 (skipping 2). */
    GatewayAuthContext_t gw_ctx;
    gateway_auth_init(&gw_ctx, 0xF0, 0x01, test_session_key);
    gateway_set_state(&gw_ctx, GW_AUTH_AUTHENTICATED);

    uint8_t frame[8] = { 0 };
    frame[0] = 0xB7; frame[1] = 0x02; frame[2] = 0x00; frame[3] = 0x00;
    /* sequence = 3 */
    frame[4] = 0x03; frame[5] = 0x00; frame[6] = 0x00; frame[7] = 0x00;

    uint8_t hmac[32];
    gateway_sign_frame(&gw_ctx, frame, sizeof(frame), hmac);

    uint8_t wire_frame[8 + 32];
    memcpy(wire_frame, frame, sizeof(frame));
    memcpy(wire_frame + sizeof(frame), hmac, 32);

    /* Verify: HMAC passes but gap is detected. */
    GatewayFrameResult_t r = gateway_verify_frame(&tracker, wire_frame, sizeof(wire_frame));
    TEST_ASSERT(r == GW_FRAME_OK);  /* HMAC is valid — gap detection is audit-log side-effect. */
    /* The audit log should contain a gap entry — verified by the audit_log test suite. */
    TEST_PASS();
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void)
{
    printf("=== test_gateway ===\n");

    /* Cache tests */
    RUN_TEST(test_cache_init);
    RUN_TEST(test_cache_write_and_read);
    RUN_TEST(test_cache_read_miss);
    RUN_TEST(test_cache_write_updates_value);
    RUN_TEST(test_cache_dirty_on_change);
    RUN_TEST(test_cache_clear_dirty);
    RUN_TEST(test_cache_get_dirty);
    RUN_TEST(test_cache_multiple_nodes_metrics);
    RUN_TEST(test_cache_capacity);
    RUN_TEST(test_cache_stats);
    RUN_TEST(test_cache_null_read);

    /* Delta filter tests */
    RUN_TEST(test_delta_filter_init);
    RUN_TEST(test_delta_filter_set_threshold);
    RUN_TEST(test_delta_filter_first_read);
    RUN_TEST(test_delta_filter_suppress_below_threshold);
    RUN_TEST(test_delta_filter_forward_above_threshold);
    RUN_TEST(test_delta_filter_no_threshold);
    RUN_TEST(test_delta_filter_different_metrics_independent);

    /* Batch forwarder tests */
    RUN_TEST(test_batch_forwarder_init);
    RUN_TEST(test_batch_forwarder_no_dirty);
    RUN_TEST(test_batch_forwarder_single_entry);
    RUN_TEST(test_batch_forwarder_multiple_entries);
    RUN_TEST(test_batch_forwarder_retry_on_fail);
    RUN_TEST(test_batch_forwarder_stats);
    RUN_TEST(test_batch_encode_decode_roundtrip);
    RUN_TEST(test_batch_decode_bad_magic);
    RUN_TEST(test_batch_decode_truncated);
    RUN_TEST(test_batch_encode_buffer_too_small);
    RUN_TEST(test_batch_forwarder_no_send_func);
    RUN_TEST(test_batch_forwarder_rejects_no_auth);
    RUN_TEST(test_batch_decode_v01_backward_compat);

    /* Gateway auth tests */
    RUN_TEST(test_gateway_auth_init);
    RUN_TEST(test_gateway_auth_state_transitions);
    RUN_TEST(test_gateway_auth_sign_requires_authenticated);
    RUN_TEST(test_gateway_auth_sign_and_verify);
    RUN_TEST(test_gateway_auth_tampered_frame_detected);
    RUN_TEST(test_gateway_auth_tampered_count_detected);
    RUN_TEST(test_gateway_auth_not_authenticated_rejected);
    RUN_TEST(test_gateway_auth_sequence_advances);
    RUN_TEST(test_gateway_auth_wrong_key_detected);
    RUN_TEST(test_gateway_v0x01_rejected_when_auth_required);
    RUN_TEST(test_gateway_v0x01_decode_vs_verify);
    RUN_TEST(test_gateway_ingest_frame_rejects_unverified);
    RUN_TEST(test_gateway_ingest_frame_accepts_verified);
    RUN_TEST(test_gateway_ingest_frame_identical_output);
    RUN_TEST(test_gateway_ingest_frame_null_edge_tracker_is_error);
    RUN_TEST(test_gateway_ingest_frame_explicit_disabled_sentinel_still_works);

    /* Integration tests */
    RUN_TEST(test_integration_auth_delta_to_batch);
    RUN_TEST(test_integration_sequence_gap_detected);

    PRINT_TEST_SUMMARY();
    return _tests_failed;
}
