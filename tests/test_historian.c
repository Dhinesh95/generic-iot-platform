/**
 * @file test_historian.c
 * @brief Tests for the Historian module (Phase 16).
 *
 * Tests cover:
 * - Basic write/read round-trip
 * - Encryption at rest (bypassing decrypt path)
 * - Ring buffer rotation under load
 * - 30-day age-based eviction
 * - Query filtering (by time, node_id, metric_id)
 * - Key separation from transport encryption
 * - Storage backend persistence
 * - Init rejects / edge cases
 */

#include "test_helpers/test_utils.h"
#include "../firmware/hub/historian.h"
#include "../firmware/shared/attestation.h"
#include "../firmware/shared/time_source.h"
#include <string.h>
#include <math.h>
#include <time.h>

/* ---------- Constants ---------- */

#define TEST_NODE_ID        0x01
#define TEST_METRIC_ID      10
#define TEST_DOMAIN_PROFILE 0  /* Home */

/* ---------- Test-only historian key (distinct from transport key) ---------- */

static const uint8_t s_test_historian_key[HISTORIAN_KEY_SIZE] = {
    0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x07, 0x18,
    0x29, 0x3A, 0x4B, 0x5C, 0x6D, 0x7E, 0x8F, 0x90
};

/* Transport encryption key — DIFFERENT from historian key. */
static const uint8_t s_transport_key[16] = {
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00
};

/* ---------- Mock storage backend ---------- */

#define MOCK_STORAGE_RECORDS  (HISTORIAN_CAPACITY + 256)
static uint8_t  s_mock_storage[MOCK_STORAGE_RECORDS][HISTORIAN_ONDISK_RECORD_SIZE];
static uint32_t s_mock_count = 0;
static uint32_t s_mock_write_index = 0;
static uint32_t s_mock_append_calls = 0;
static uint32_t s_mock_load_calls = 0;

static bool mock_append(uint32_t index, const uint8_t *record,
                        uint32_t count, uint32_t write_index)
{
    s_mock_append_calls++;
    s_mock_count = count;
    s_mock_write_index = write_index;
    if (index < MOCK_STORAGE_RECORDS) {
        memcpy(s_mock_storage[index], record, HISTORIAN_ONDISK_RECORD_SIZE);
    }
    return true;
}

static bool mock_load(uint8_t *records, uint32_t max_records,
                      uint32_t *out_count, uint32_t *out_write_index)
{
    s_mock_load_calls++;
    uint32_t to_copy = (s_mock_count < max_records) ? s_mock_count : max_records;
    for (uint32_t i = 0; i < to_copy; i++) {
        memcpy(records + i * HISTORIAN_ONDISK_RECORD_SIZE,
               s_mock_storage[i], HISTORIAN_ONDISK_RECORD_SIZE);
    }
    *out_count = to_copy;
    *out_write_index = s_mock_write_index;
    return true;
}

static HistorianStorage_t s_mock_storage_backend = {
    .append = mock_append,
    .load = mock_load
};

/* ---------- Helper: reset all state ---------- */

static void reset_all(void)
{
    memset(s_mock_storage, 0, sizeof(s_mock_storage));
    s_mock_count = 0;
    s_mock_write_index = 0;
    s_mock_append_calls = 0;
    s_mock_load_calls = 0;

    time_source_init();
    time_source_mock_set(1000000, true);  /* 1 million ms = ~16 minutes */

    historian_set_key(s_test_historian_key, HISTORIAN_KEY_SIZE);
    historian_init();
}

/* ================================================================
 * TEST 1: Basic write/read round-trip
 * ================================================================ */

static int test_write_read_roundtrip(void)
{
    reset_all();

    HistorianRecord_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.timestamp_ms      = 1000000;
    rec.value             = 42.5f;
    rec.node_id           = TEST_NODE_ID;
    rec.metric_id         = TEST_METRIC_ID;
    rec.domain_profile_id = TEST_DOMAIN_PROFILE;

    TEST_ASSERT(historian_write(&rec));
    TEST_ASSERT_EQUAL(1, historian_count());

    HistorianQuery_t query;
    memset(&query, 0, sizeof(query));

    HistorianRecord_t out[4];
    uint32_t found = historian_read(&query, out, 4);

    TEST_ASSERT_EQUAL(1, found);
    TEST_ASSERT_EQUAL(1000000, out[0].timestamp_ms);
    TEST_ASSERT_EQUAL_FLOAT(42.5f, out[0].value, 0.001);
    TEST_ASSERT_EQUAL(TEST_NODE_ID, out[0].node_id);
    TEST_ASSERT_EQUAL(TEST_METRIC_ID, out[0].metric_id);
    TEST_ASSERT_EQUAL(TEST_DOMAIN_PROFILE, out[0].domain_profile_id);

    TEST_PASS();
}

/* ================================================================
 * TEST 2: Encryption at rest — raw storage is NOT plaintext
 *
 * This is the critical test proving data-at-rest encryption works.
 * We write a record, then read the raw on-disk bytes directly from
 * the storage backend (bypassing historian_read's decrypt path).
 * The raw bytes must NOT match the plaintext record.
 * ================================================================ */

static int test_encryption_at_rest_not_plaintext(void)
{
    reset_all();

    HistorianRecord_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.timestamp_ms = 0xDEADBEEF;
    rec.value        = 3.14f;
    rec.node_id      = 0x42;
    rec.metric_id    = 7;

    TEST_ASSERT(historian_write(&rec));

    /* Flush to storage so we can read raw on-disk bytes. */
    historian_flush(rec.timestamp_ms);

    /* Read raw on-disk bytes from the mock storage backend.
     * These are the encrypted bytes BEFORE decryption. */
    HistorianRecord_t raw_ondisk;
    memcpy(&raw_ondisk, s_mock_storage[0], sizeof(HistorianRecord_t));

    /* The raw on-disk bytes must NOT match the plaintext.
     * If they do, encryption is broken. */
    TEST_ASSERT(memcmp(&raw_ondisk, &rec, sizeof(HistorianRecord_t)) != 0);

    /* Verify the value field is not readable as plaintext. */
    float raw_value;
    memcpy(&raw_value, &raw_ondisk.value, sizeof(float));
    /* The encrypted value should not equal 3.14f (with overwhelming probability). */
    TEST_ASSERT(fabsf(raw_value - 3.14f) > 0.01f);

    /* Verify the node_id field is not readable as plaintext. */
    TEST_ASSERT(raw_ondisk.node_id != 0x42);

    /* Verify decryption via historian_read still works correctly. */
    HistorianQuery_t query;
    memset(&query, 0, sizeof(query));
    HistorianRecord_t out[1];
    uint32_t found = historian_read(&query, out, 1);
    TEST_ASSERT_EQUAL(1, found);
    TEST_ASSERT_EQUAL_FLOAT(3.14f, out[0].value, 0.001);
    TEST_ASSERT_EQUAL(0x42, out[0].node_id);

    printf("  Raw on-disk bytes: value=%.2f (should NOT be 3.14)\n", raw_value);
    TEST_PASS();
}

/* ================================================================
 * TEST 3: Key separation — transport key cannot decrypt historian
 * ================================================================ */

static int test_key_separation_from_transport(void)
{
    reset_all();

    /* Write a record with the historian key. */
    HistorianRecord_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.timestamp_ms = 2000000;
    rec.value        = 99.9f;
    rec.node_id      = 0x05;
    rec.metric_id    = 3;

    TEST_ASSERT(historian_write(&rec));

    /* Now re-init the historian with the TRANSPORT key instead. */
    historian_set_key(s_transport_key, 16);
    /* Note: we don't re-init — we just change the key to simulate
     * what would happen if someone tried to use the transport key
     * to read historian data. */

    /* Re-init with the wrong key (transport key). */
    /* We need to reload from storage to test this. But since the
     * historian encrypts on write, we need to write with one key
     * and read with another. Let's do that directly. */

    /* Actually, the simplest test: write with historian key, reinit
     * with transport key, and verify read fails. */
    historian_set_key(s_test_historian_key, HISTORIAN_KEY_SIZE);
    historian_init();

    TEST_ASSERT(historian_write(&rec));

    /* Now set the wrong key and re-init from storage. */
    HistorianStorage_t storage = s_mock_storage_backend;
    historian_set_storage(&storage);
    historian_set_key(s_transport_key, 16);
    historian_init();

    /* Read should fail (decrypt errors) because wrong key. */
    HistorianQuery_t query;
    memset(&query, 0, sizeof(query));
    HistorianRecord_t out[1];
    uint32_t found = historian_read(&query, out, 1);

    /* With the wrong key, either 0 records are returned (decrypt fails)
     * or the records are garbage. Either way, not the original value. */
    if (found > 0) {
        TEST_ASSERT(fabsf(out[0].value - 99.9f) > 0.01f);
    }
    /* found == 0 is also acceptable — decrypt failed silently. */

    printf("  Wrong-key read: found=%lu, value=%.2f (should NOT be 99.90)\n",
           (unsigned long)found, (found > 0) ? out[0].value : 0.0f);

    /* Restore correct key for subsequent tests. */
    historian_set_storage(NULL);
    historian_set_key(s_test_historian_key, HISTORIAN_KEY_SIZE);
    historian_init();

    TEST_PASS();
}

/* ================================================================
 * TEST 4: Ring buffer rotation — write beyond capacity
 * ================================================================ */

static int test_rotation_under_load(void)
{
    /* Use a small capacity for this test. */
    /* We can't change HISTORIAN_CAPACITY at runtime, so we'll write
     * enough records to exceed a reasonable number and verify rotation. */
    reset_all();

    uint32_t capacity = historian_capacity();
    uint32_t extra = 100;

    /* Write capacity + extra records. */
    for (uint32_t i = 0; i < capacity + extra; i++) {
        HistorianRecord_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.timestamp_ms = 1000000 + (uint64_t)i * 1000;  /* 1 second apart */
        rec.value        = (float)i;
        rec.node_id      = (uint8_t)(i % 10 + 1);
        rec.metric_id    = (uint8_t)(i % 5 + 1);
        rec.domain_profile_id = TEST_DOMAIN_PROFILE;

        TEST_ASSERT(historian_write(&rec));
    }

    /* Count should be at capacity, not beyond. */
    TEST_ASSERT_EQUAL(capacity, historian_count());

    /* The oldest records (0..extra-1) should have been evicted.
     * Read back and verify the first record has value >= extra. */
    HistorianQuery_t query;
    memset(&query, 0, sizeof(query));
    HistorianRecord_t out[1];
    uint32_t found = historian_read(&query, out, 1);

    TEST_ASSERT_EQUAL(1, found);
    /* The first record should be one of the later-written records. */
    TEST_ASSERT(out[0].value >= (float)extra);

    const HistorianStats_t *stats = historian_get_stats();
    TEST_ASSERT(stats->records_evicted >= extra);

    printf("  Rotation: wrote %lu records, capacity=%lu, evicted=%lu, first_value=%.0f\n",
           (unsigned long)(capacity + extra), (unsigned long)capacity,
           (unsigned long)stats->records_evicted, (double)out[0].value);

    TEST_PASS();
}

/* ================================================================
 * TEST 5: 30-day age-based eviction
 * ================================================================ */

static int test_30day_eviction(void)
{
    reset_all();

    /* Write a record at "now" (1,000,000 ms). */
    HistorianRecord_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.timestamp_ms = 1000000;
    rec.value        = 1.0f;
    rec.node_id      = 1;
    rec.metric_id    = 1;
    rec.domain_profile_id = TEST_DOMAIN_PROFILE;

    TEST_ASSERT(historian_write(&rec));
    TEST_ASSERT_EQUAL(1, historian_count());

    /* Advance time by 31 days (> 30-day window). */
    uint64_t advance_ms = 31ULL * 24 * 60 * 60 * 1000;

    /* Write another record at the new time. This should trigger
     * eviction of the old record (31 days old). */
    rec.timestamp_ms = 1000000 + advance_ms;
    rec.value        = 2.0f;

    TEST_ASSERT(historian_write(&rec));

    /* The old record should have been evicted by age. */
    HistorianQuery_t query;
    memset(&query, 0, sizeof(query));
    HistorianRecord_t out[4];
    uint32_t found = historian_read(&query, out, 4);

    /* Only the new record should remain. */
    TEST_ASSERT_EQUAL(1, found);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, out[0].value, 0.001);

    const HistorianStats_t *stats = historian_get_stats();
    TEST_ASSERT(stats->records_evicted >= 1);

    printf("  30-day eviction: evicted=%lu, remaining=%lu\n",
           (unsigned long)stats->records_evicted,
           (unsigned long)historian_count());

    TEST_PASS();
}

/* ================================================================
 * TEST 6: Query filter — by node_id
 * ================================================================ */

static int test_query_by_node_id(void)
{
    reset_all();

    /* Write records from 3 different nodes. */
    for (int i = 0; i < 9; i++) {
        HistorianRecord_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.timestamp_ms = 1000000 + i * 1000;
        rec.value        = (float)(i + 1) * 10.0f;
        rec.node_id      = (uint8_t)(i % 3 + 1);  /* Nodes 1, 2, 3 */
        rec.metric_id    = 1;
        rec.domain_profile_id = TEST_DOMAIN_PROFILE;
        TEST_ASSERT(historian_write(&rec));
    }

    /* Query for node_id = 2 only. */
    HistorianQuery_t query;
    memset(&query, 0, sizeof(query));
    query.node_id = 2;

    HistorianRecord_t out[10];
    uint32_t found = historian_read(&query, out, 10);

    /* Should get 3 records (indices 1, 4, 7 → node_id = 2). */
    TEST_ASSERT_EQUAL(3, found);
    for (uint32_t i = 0; i < found; i++) {
        TEST_ASSERT_EQUAL(2, out[i].node_id);
    }

    TEST_PASS();
}

/* ================================================================
 * TEST 7: Query filter — by time range
 * ================================================================ */

static int test_query_by_time_range(void)
{
    reset_all();

    /* Write records at different times. */
    for (int i = 0; i < 5; i++) {
        HistorianRecord_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.timestamp_ms = 1000000 + (uint64_t)i * 100000;  /* 100s apart */
        rec.value        = (float)i;
        rec.node_id      = 1;
        rec.metric_id    = 1;
        rec.domain_profile_id = TEST_DOMAIN_PROFILE;
        TEST_ASSERT(historian_write(&rec));
    }

    /* Query: time range [1100000, 1300000] → should get records 1, 2, 3. */
    HistorianQuery_t query;
    memset(&query, 0, sizeof(query));
    query.time_start_ms = 1100000;
    query.time_end_ms   = 1300000;

    HistorianRecord_t out[10];
    uint32_t found = historian_read(&query, out, 10);

    TEST_ASSERT_EQUAL(3, found);
    TEST_ASSERT_EQUAL(1100000, out[0].timestamp_ms);
    TEST_ASSERT_EQUAL(1200000, out[1].timestamp_ms);
    TEST_ASSERT_EQUAL(1300000, out[2].timestamp_ms);

    TEST_PASS();
}

/* ================================================================
 * TEST 8: Query filter — by metric_id
 * ================================================================ */

static int test_query_by_metric_id(void)
{
    reset_all();

    for (int i = 0; i < 6; i++) {
        HistorianRecord_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.timestamp_ms = 1000000 + i * 1000;
        rec.value        = (float)i;
        rec.node_id      = 1;
        rec.metric_id    = (uint8_t)(i % 2 + 1);  /* Metrics 1, 2 */
        rec.domain_profile_id = TEST_DOMAIN_PROFILE;
        TEST_ASSERT(historian_write(&rec));
    }

    HistorianQuery_t query;
    memset(&query, 0, sizeof(query));
    query.metric_id = 2;

    HistorianRecord_t out[10];
    uint32_t found = historian_read(&query, out, 10);

    TEST_ASSERT_EQUAL(3, found);
    for (uint32_t i = 0; i < found; i++) {
        TEST_ASSERT_EQUAL(2, out[i].metric_id);
    }

    TEST_PASS();
}

/* ================================================================
 * TEST 9: Storage persistence — save/load round-trip
 * ================================================================ */

static int test_storage_persistence(void)
{
    reset_all();

    /* Register storage backend. */
    historian_set_storage(&s_mock_storage_backend);

    /* Write 5 records. */
    for (int i = 0; i < 5; i++) {
        HistorianRecord_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.timestamp_ms = 1000000 + i * 1000;
        rec.value        = (float)(i + 1) * 7.0f;
        rec.node_id      = 1;
        rec.metric_id    = 1;
        rec.domain_profile_id = TEST_DOMAIN_PROFILE;
        TEST_ASSERT(historian_write(&rec));
    }

    /* Force flush to storage. */
    historian_flush(1000000 + 5000);
    TEST_ASSERT(s_mock_append_calls >= 5);

    /* Simulate reboot: re-init historian (should load from storage). */
    historian_init();

    TEST_ASSERT_EQUAL(5, historian_count());
    TEST_ASSERT(s_mock_load_calls >= 1);

    /* Read back and verify values survived the "reboot". */
    HistorianQuery_t query;
    memset(&query, 0, sizeof(query));
    HistorianRecord_t out[5];
    uint32_t found = historian_read(&query, out, 5);

    TEST_ASSERT_EQUAL(5, found);
    TEST_ASSERT_EQUAL_FLOAT(7.0f,  out[0].value, 0.001);
    TEST_ASSERT_EQUAL_FLOAT(14.0f, out[1].value, 0.001);
    TEST_ASSERT_EQUAL_FLOAT(21.0f, out[2].value, 0.001);
    TEST_ASSERT_EQUAL_FLOAT(28.0f, out[3].value, 0.001);
    TEST_ASSERT_EQUAL_FLOAT(35.0f, out[4].value, 0.001);

    historian_set_storage(NULL);
    TEST_PASS();
}

/* ================================================================
 * TEST 10: Init rejects NULL / edge cases
 * ================================================================ */

static int test_init_null_key(void)
{
    /* Write without setting a key should fail. */
    historian_set_key(NULL, 0);
    historian_init();

    HistorianRecord_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.timestamp_ms = 1000000;
    rec.value        = 1.0f;

    TEST_ASSERT(!historian_write(&rec));
    TEST_ASSERT_EQUAL(0, historian_count());

    /* Restore key for clean state. */
    historian_set_key(s_test_historian_key, HISTORIAN_KEY_SIZE);
    TEST_PASS();
}

static int test_read_null_params(void)
{
    reset_all();

    HistorianRecord_t out[1];
    TEST_ASSERT_EQUAL(0, historian_read(NULL, out, 1));
    TEST_ASSERT_EQUAL(0, historian_read(&(HistorianQuery_t){0}, NULL, 1));
    TEST_ASSERT_EQUAL(0, historian_read(&(HistorianQuery_t){0}, out, 0));

    TEST_PASS();
}

static int test_write_null_record(void)
{
    reset_all();
    TEST_ASSERT(!historian_write(NULL));
    TEST_PASS();
}

/* ================================================================
 * TEST 11: Stats tracking
 * ================================================================ */

static int test_stats_tracking(void)
{
    reset_all();

    const HistorianStats_t *stats = historian_get_stats();
    TEST_ASSERT_EQUAL(0, stats->records_written);
    TEST_ASSERT_EQUAL(0, stats->records_evicted);
    TEST_ASSERT_EQUAL(0, stats->write_errors);

    /* Write a record. */
    HistorianRecord_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.timestamp_ms = 1000000;
    rec.value        = 1.0f;

    TEST_ASSERT(historian_write(&rec));

    stats = historian_get_stats();
    TEST_ASSERT_EQUAL(1, stats->records_written);
    TEST_ASSERT_EQUAL(0, stats->records_evicted);

    /* Reset stats. */
    historian_reset_stats();
    stats = historian_get_stats();
    TEST_ASSERT_EQUAL(0, stats->records_written);

    TEST_PASS();
}

/* ================================================================
 * TEST 12: Capacity and ondisk_record_size getters
 * ================================================================ */

static int test_getters(void)
{
    TEST_ASSERT(historian_capacity() > 0);
    TEST_ASSERT_EQUAL(HISTORIAN_ONDISK_RECORD_SIZE, historian_ondisk_record_size());
    TEST_ASSERT_EQUAL(40, historian_ondisk_record_size());
    TEST_PASS();
}

/* ================================================================
 * TEST 13: Write timing — measure actual historian_write() duration
 * ================================================================ */

static int test_write_timing(void)
{
    reset_all();

    /* Warm up: do a few writes to fill any caches. */
    for (int i = 0; i < 10; i++) {
        HistorianRecord_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.timestamp_ms = 1000000 + i;
        rec.value = (float)i;
        rec.node_id = 1;
        rec.metric_id = 1;
        historian_write(&rec);
    }

    /* Measure: use enough iterations to exceed clock() resolution.
     * Windows CLOCKS_PER_SEC=1000 (1ms granularity), so we need
     * enough iterations to take >1ms total. 10,000 iterations
     * should take ~50ms at 5us/write, well above resolution. */
    #define TIMING_ITERATIONS 10000
    clock_t start = clock();
    for (int i = 0; i < TIMING_ITERATIONS; i++) {
        HistorianRecord_t rec;
        memset(&rec, 0, sizeof(rec));
        rec.timestamp_ms = 2000000 + i;
        rec.value = (float)(i + 100);
        rec.node_id = (uint8_t)(i % 10 + 1);
        rec.metric_id = (uint8_t)(i % 5 + 1);
        rec.domain_profile_id = TEST_DOMAIN_PROFILE;
        historian_write(&rec);
    }
    clock_t end = clock();

    double elapsed_sec = (double)(end - start) / CLOCKS_PER_SEC;
    double avg_us = (elapsed_sec * 1000000.0) / TIMING_ITERATIONS;

    printf("  %d writes: %.3f sec total, %.1f us/write (avg)\n",
           TIMING_ITERATIONS, elapsed_sec, avg_us);
    printf("  CLOCKS_PER_SEC = %ld\n", (long)CLOCKS_PER_SEC);

    /* Sanity: average must be positive and not absurdly large.
     * Native gcc: typically 5-50 us/write (AES-128-CCM encrypt).
     * ESP32 without crypto accel: 50-200 us/write.
     * ESP32 with crypto accel: 10-50 us/write. */
    TEST_ASSERT(avg_us > 0.0);
    TEST_ASSERT(avg_us < 10000.0);  /* < 10ms — absurdly slow if exceeded */

    /* Compare against ingestion pipeline budget:
     * ingestion_handler_poll() runs at 1 Hz (1000 ms period).
     * One historian_write() at avg_us microseconds consumes:
     *   avg_us / 1,000,000 of the 1-second budget.
     * At 50 us/write: 0.005% of budget — negligible.
     * At 200 us/write: 0.02% of budget — still negligible.
     * Even at 1 ms/write: 0.1% of budget — acceptable. */
    double budget_pct = (avg_us / 1000000.0) * 100.0;
    printf("  Ingestion budget impact: %.4f%% of 1-second poll cycle\n",
           budget_pct);

    /* Limitation note: native gcc timing differs from ESP32.
     * ESP32 has optional AES hardware acceleration (AES-128-CCM).
     * If hardware accel is available, ESP32 timing may be FASTER.
     * If not available, ESP32 timing may be 2-10× slower.
     * This test measures native/gcc performance only. */

    TEST_PASS();
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void)
{
    int _total = 0, _passed = 0, _failed = 0;

    printf("\n========================================\n");
    printf("Historian Module Tests (Phase 16)\n");
    printf("========================================\n\n");

    printf("--- Core Tests ---\n");
    RUN_TEST(test_init_null_key);
    RUN_TEST(test_read_null_params);
    RUN_TEST(test_write_null_record);
    RUN_TEST(test_getters);
    RUN_TEST(test_stats_tracking);

    printf("\n--- Write/Read Tests ---\n");
    RUN_TEST(test_write_read_roundtrip);
    RUN_TEST(test_rotation_under_load);
    RUN_TEST(test_30day_eviction);

    printf("\n--- Encryption Tests ---\n");
    RUN_TEST(test_encryption_at_rest_not_plaintext);
    RUN_TEST(test_key_separation_from_transport);

    printf("\n--- Query Tests ---\n");
    RUN_TEST(test_query_by_node_id);
    RUN_TEST(test_query_by_time_range);
    RUN_TEST(test_query_by_metric_id);

    printf("\n--- Persistence Tests ---\n");
    RUN_TEST(test_storage_persistence);

    printf("\n--- Timing Tests (Phase 16.1) ---\n");
    RUN_TEST(test_write_timing);

    PRINT_TEST_SUMMARY();
}
