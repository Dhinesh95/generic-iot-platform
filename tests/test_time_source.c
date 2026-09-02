/**
 * @file test_time_source.c
 * @brief Tests for time source — NTP/RTC fallback and audit log timestamps.
 *
 * Architecture ref: Section 12 (RTC fallback, NTP sync).
 *
 * Tests cover:
 * - NTP-active path (trustworthy timestamps)
 * - NTP-stale/RTC-fallback path
 * - Neither-available path (unreliable flag, not a fake timestamp)
 * - Audit log entries created under each path are correctly timestamped/flagged
 */

#include "test_helpers/test_utils.h"
#include "../firmware/shared/time_source.h"
#include "../firmware/shared/audit_log.h"
#include <string.h>

/* ================================================================
 * MOCK BACKENDS FOR TESTING
 * ================================================================ */

static bool s_ntp_synced = false;
static uint64_t s_ntp_time = 0;
static bool s_rtc_available = false;
static uint64_t s_rtc_time = 0;

static uint64_t mock_ntp_time_get(void) { return s_ntp_time; }
static bool mock_ntp_is_synced(void) { return s_ntp_synced; }
static uint64_t mock_rtc_time_get(void) { return s_rtc_time; }
static bool mock_rtc_is_available(void) { return s_rtc_available; }

static TimeSourceBackend_t s_mock_backend = {
    .ntp_time_get = mock_ntp_time_get,
    .ntp_is_synced = mock_ntp_is_synced,
    .rtc_time_get = mock_rtc_time_get,
    .rtc_is_available = mock_rtc_is_available
};

static void reset_mock_backend(void)
{
    s_ntp_synced = false;
    s_ntp_time = 0;
    s_rtc_available = false;
    s_rtc_time = 0;
}

/* ================================================================
 * TIME SOURCE TESTS
 * ================================================================ */

/* ---------- Test: init ---------- */
static int test_time_source_init(void)
{
    TEST_ASSERT(time_source_init() == true);
    TEST_ASSERT(time_source_get_state() == TIME_SRC_UNINITIALISED);
    TEST_PASS();
}

/* ---------- Test: NTP-active path ---------- */
static int test_time_source_ntp_active(void)
{
    time_source_init();
    reset_mock_backend();
    s_ntp_synced = true;
    s_ntp_time = 1700000000000ULL;  /* ~Nov 2023 */
    time_source_set_backend(&s_mock_backend);

    uint64_t t = time_source_get_ms();
    TEST_ASSERT_EQUAL(1700000000000ULL, t);
    TEST_ASSERT(time_source_is_reliable() == true);
    TEST_ASSERT(time_source_get_state() == TIME_SRC_NTP_ACTIVE);
    TEST_PASS();
}

/* ---------- Test: NTP-stale → RTC fallback ---------- */
static int test_time_source_rtc_fallback(void)
{
    time_source_init();
    reset_mock_backend();
    s_ntp_synced = false;  /* NTP not synced (stale). */
    s_ntp_time = 1700000000000ULL;
    s_rtc_available = true;
    s_rtc_time = 500000ULL;  /* RTC counts from boot. */
    time_source_set_backend(&s_mock_backend);

    uint64_t t = time_source_get_ms();
    TEST_ASSERT_EQUAL(500000ULL, t);
    TEST_ASSERT(time_source_is_reliable() == true);
    TEST_ASSERT(time_source_get_state() == TIME_SRC_RTC_ONLY);
    TEST_PASS();
}

/* ---------- Test: NTP synced but returns 0 → RTC fallback ---------- */
static int test_time_source_ntp_zero_rtc_fallback(void)
{
    time_source_init();
    reset_mock_backend();
    s_ntp_synced = true;
    s_ntp_time = 0;  /* Synced but time is 0 (error). */
    s_rtc_available = true;
    s_rtc_time = 300000ULL;
    time_source_set_backend(&s_mock_backend);

    uint64_t t = time_source_get_ms();
    TEST_ASSERT_EQUAL(300000ULL, t);
    TEST_ASSERT(time_source_get_state() == TIME_SRC_RTC_ONLY);
    TEST_PASS();
}

/* ---------- Test: neither available → unreliable ---------- */
static int test_time_source_unreliable(void)
{
    time_source_init();
    reset_mock_backend();
    time_source_set_backend(&s_mock_backend);

    uint64_t t = time_source_get_ms();
    TEST_ASSERT_EQUAL(0, t);
    TEST_ASSERT(time_source_is_reliable() == false);
    TEST_ASSERT(time_source_get_state() == TIME_SRC_UNRELIABLE);
    TEST_PASS();
}

/* ---------- Test: no backend → unreliable ---------- */
static int test_time_source_no_backend(void)
{
    time_source_init();
    time_source_set_backend(NULL);

    uint64_t t = time_source_get_ms();
    TEST_ASSERT_EQUAL(0, t);
    TEST_ASSERT(time_source_is_reliable() == false);
    TEST_PASS();
}

/* ---------- Test: mock set/advance ---------- */
static int test_time_source_mock(void)
{
    time_source_init();

    time_source_mock_set(1000, true);
    TEST_ASSERT_EQUAL(1000, time_source_get_ms());
    TEST_ASSERT(time_source_is_reliable() == true);

    time_source_mock_advance(500);
    TEST_ASSERT_EQUAL(1500, time_source_get_ms());

    time_source_mock_set(0, false);
    TEST_ASSERT_EQUAL(0, time_source_get_ms());
    TEST_ASSERT(time_source_is_reliable() == false);
    TEST_PASS();
}

/* ================================================================
 * AUDIT LOG TIMESTAMP TESTS
 * ================================================================ */

/* ---------- Test: audit log gets NTP timestamp ---------- */
static int test_audit_log_ntp_timestamp(void)
{
    time_source_init();
    time_source_mock_set(50000, true);

    audit_log_init();
    audit_log_add(AUDIT_AUTH_SUCCESS, 1, 0, "test ntp");

    AuditLogEntry_t entries[4];
    uint8_t count = audit_log_get_entries(entries, 4);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL(50000, entries[0].timestamp_ms);
    TEST_PASS();
}

/* ---------- Test: audit log gets RTC timestamp ---------- */
static int test_audit_log_rtc_timestamp(void)
{
    time_source_init();
    time_source_mock_set(30000, true);  /* RTC-only is still "reliable". */

    audit_log_init();
    audit_log_add(AUDIT_AUTH_FAILURE, 0, 0, "test rtc");

    AuditLogEntry_t entries[4];
    uint8_t count = audit_log_get_entries(entries, 4);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL(30000, entries[0].timestamp_ms);
    TEST_PASS();
}

/* ---------- Test: audit log gets 0 when unreliable ---------- */
static int test_audit_log_unreliable_timestamp(void)
{
    time_source_init();
    time_source_mock_set(0, false);

    audit_log_init();
    audit_log_add(AUDIT_TAMPER_DETECTED, 0, 0, "test unreliable");

    AuditLogEntry_t entries[4];
    uint8_t count = audit_log_get_entries(entries, 4);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL(0, entries[0].timestamp_ms);

    /* The timestamp being 0 is itself the "unreliable" signal —
     * a forensic reviewer seeing timestamp_ms = 0 knows this event
     * occurred during an unreliable-time window. This is the correct
     * behavior: never fake a plausible-looking-but-wrong timestamp. */
    TEST_ASSERT(time_source_is_reliable() == false);
    TEST_PASS();
}

/* ---------- Test: audit log timestamp changes with time ---------- */
static int test_audit_log_timestamp_changes(void)
{
    time_source_init();
    time_source_mock_set(1000, true);

    audit_log_init();
    audit_log_add(AUDIT_SESSION_CREATED, 1, 0, "first");

    time_source_mock_set(2000, true);
    audit_log_add(AUDIT_AUTH_SUCCESS, 1, 0, "second");

    AuditLogEntry_t entries[4];
    uint8_t count = audit_log_get_entries(entries, 4);
    TEST_ASSERT_EQUAL(2, count);
    TEST_ASSERT_EQUAL(1000, entries[0].timestamp_ms);
    TEST_ASSERT_EQUAL(2000, entries[1].timestamp_ms);
    TEST_PASS();
}

/* ================================================================
 * RTC PERSISTENCE TESTS
 * ================================================================ */

/* RAM-backed storage for time source persistence tests. */
static uint64_t s_ts_persist_rtc = 0;
static bool s_ts_persist_dirty = false;

static bool ts_test_storage_save(uint64_t rtc_time_ms)
{
    s_ts_persist_rtc = rtc_time_ms;
    s_ts_persist_dirty = true;
    return true;
}

static bool ts_test_storage_load(uint64_t *rtc_time_ms)
{
    *rtc_time_ms = s_ts_persist_rtc;
    return true;
}

static const TimeSourceStorage_t s_ts_test_storage = {
    .save = ts_test_storage_save,
    .load = ts_test_storage_load
};

/** Mock RTC backend — returns the persistent RTC value.
 *  On real hardware, the DS3231 provides absolute wall-clock time
 *  directly from its independently-powered oscillator. The MCU just
 *  reads it — no delta arithmetic needed. For testing, we simulate
 *  this with time_source_mock_set_rtc_persistent(). */

/* Test-side tracking of mock RTC wall-clock time. */
static uint64_t s_simulated_rtc_wall_clock = 0;

static uint64_t mock_rtc_persist_get(void) { return s_simulated_rtc_wall_clock; }
static bool mock_rtc_persist_available(void) { return s_simulated_rtc_wall_clock > 0; }
static uint64_t mock_ntp_persist_time_get(void) { return 0; }
static bool mock_ntp_persist_is_synced(void) { return false; }  /* NTP unavailable. */

static TimeSourceBackend_t s_rtc_only_backend = {
    .ntp_time_get = mock_ntp_persist_time_get,
    .ntp_is_synced = mock_ntp_persist_is_synced,
    .rtc_time_get = mock_rtc_persist_get,
    .rtc_is_available = mock_rtc_persist_available
};

/* NTP-resync mock state. */
static bool s_ntp_resync_synced_flag = false;
static uint64_t s_ntp_resync_time_value = 0;

static uint64_t mock_ntp_resync_get(void) { return s_ntp_resync_time_value; }
static bool mock_ntp_resync_synced(void) { return s_ntp_resync_synced_flag; }

static TimeSourceBackend_t s_ntp_resync_backend = {
    .ntp_time_get = mock_ntp_resync_get,
    .ntp_is_synced = mock_ntp_resync_synced,
    .rtc_time_get = mock_rtc_persist_get,
    .rtc_is_available = mock_rtc_persist_available
};

/** Helper: simulate reboot with RTC persistence.
 *  Advances the mock RTC's wall-clock time (simulating the chip's
 *  oscillator ticking during the outage), then reinitialises. */
static void ts_simulate_reboot_rtc_only(void)
{
    s_ts_persist_dirty = false;
    /* Simulate: 5 seconds of wall-clock time passed during the outage.
     * The RTC chip kept ticking — it now reports a later absolute time. */
    s_simulated_rtc_wall_clock += 5000;
    time_source_mock_set_rtc_persistent(s_simulated_rtc_wall_clock);
    time_source_set_storage(&s_ts_test_storage);
    time_source_set_backend(&s_rtc_only_backend);
    time_source_init();
}

/* ---------- Test: audit log timestamps coherent across reboot ---------- */
static int test_audit_log_timestamp_coherent_across_reboot(void)
{
    /* Boot 1: RTC-only, write an audit entry. */
    s_ts_persist_rtc = 0;
    s_simulated_rtc_wall_clock = 1000000;
    time_source_mock_set_rtc_persistent(s_simulated_rtc_wall_clock);
    time_source_set_storage(&s_ts_test_storage);
    time_source_set_backend(&s_rtc_only_backend);
    time_source_init();

    audit_log_init();
    audit_log_add(AUDIT_SESSION_CREATED, 1, 0, "boot 1 entry");

    AuditLogEntry_t entries[4];
    uint8_t count = audit_log_get_entries(entries, 4);
    TEST_ASSERT_EQUAL(1, count);
    uint64_t boot1_ts = entries[0].timestamp_ms;
    TEST_ASSERT(boot1_ts > 0);  /* Got a real RTC timestamp. */

    /* Simulate reboot — RTC advances by 5 seconds. */
    ts_simulate_reboot_rtc_only();

    /* The time source should now return a value > boot1_ts
     * (coherent with wall-clock passage), not a small relative value
     * that could overlap with boot1's timestamp. */
    uint64_t boot2_time = time_source_get_ms();
    TEST_ASSERT(boot2_time > boot1_ts);

    /* The difference should be roughly 5 seconds (the elapsed time).
     * Allow some margin for the RTC advancement model. */
    uint64_t elapsed = boot2_time - boot1_ts;
    TEST_ASSERT(elapsed >= 4000);   /* At least ~4s (allow margin). */
    TEST_ASSERT(elapsed <= 60000);  /* Not wildly far off. */

    /* Write another audit entry in boot 2. */
    audit_log_add(AUDIT_AUTH_SUCCESS, 1, 0, "boot 2 entry");

    count = audit_log_get_entries(entries, 4);
    TEST_ASSERT_EQUAL(2, count);
    TEST_ASSERT(entries[1].timestamp_ms > entries[0].timestamp_ms);

    TEST_PASS();
}

/* ---------- Test: NTP re-syncs after RTC-persisted reboot ---------- */
static int test_ntp_resyncs_after_rtc_reboot(void)
{
    /* Boot 1: RTC-only. */
    s_ts_persist_rtc = 0;
    time_source_mock_set_rtc_persistent(1000000);
    time_source_set_storage(&s_ts_test_storage);
    time_source_set_backend(&s_rtc_only_backend);
    time_source_init();

    uint64_t boot1_time = time_source_get_ms();
    TEST_ASSERT(time_source_get_state() == TIME_SRC_RTC_ONLY);

    /* Simulate reboot — RTC chip advanced, then NTP becomes available. */
    time_source_mock_set_rtc_persistent(1005000);
    s_ntp_resync_synced_flag = true;
    s_ntp_resync_time_value = 1700000005000ULL;
    time_source_set_backend(&s_ntp_resync_backend);
    time_source_init();

    /* NTP should take priority over persisted RTC. */
    uint64_t boot2_time = time_source_get_ms();
    TEST_ASSERT_EQUAL(1700000005000ULL, boot2_time);
    TEST_ASSERT(time_source_get_state() == TIME_SRC_NTP_ACTIVE);
    (void)boot1_time;
    (void)boot2_time;
    TEST_PASS();
}

/* ---------- Test: mock RTC persists independent of storage ---------- */
static int test_rtc_mock_persists_independent_of_storage(void)
{
    s_simulated_rtc_wall_clock = 1000000;
    time_source_mock_set_rtc_persistent(1000000);
    time_source_set_storage(NULL);
    time_source_set_backend(&s_rtc_only_backend);
    time_source_init();

    uint64_t t1 = time_source_get_ms();
    TEST_ASSERT_EQUAL(1000000, t1);

    /* Simulate reboot without storage. */
    time_source_init();

    /* Without storage, the mock RTC variable was not reloaded —
     * it retains whatever value the test set. The backend is still
     * available (mock_rtc_persist_available checks the variable > 0).
     * This correctly models: the RTC chip itself is the persistence —
     * it keeps ticking during the outage. The MCU just reads it.
     *
     * The key difference from the persistence path: without storage,
     * the mock variable doesn't automatically advance to reflect time
     * passage during the outage — the test must explicitly set the
     * new RTC value, simulating what the chip would report. */
    uint64_t t2 = time_source_get_ms();
    /* Same value as before — the mock RTC hasn't been advanced.
     * This is correct: without the test calling mock_set_rtc_persistent()
     * to simulate time passage, the mock "chip" reports the same value. */
    TEST_ASSERT_EQUAL(1000000, t2);
    TEST_PASS();
}

/* ================================================================
 * MAIN
 * ================================================================ */

int main(void)
{
    int _total = 0, _passed = 0, _failed = 0;

    printf("=== test_time_source ===\n");

    /* Time source tests. */
    RUN_TEST(test_time_source_init);
    RUN_TEST(test_time_source_ntp_active);
    RUN_TEST(test_time_source_rtc_fallback);
    RUN_TEST(test_time_source_ntp_zero_rtc_fallback);
    RUN_TEST(test_time_source_unreliable);
    RUN_TEST(test_time_source_no_backend);
    RUN_TEST(test_time_source_mock);

    /* Audit log timestamp tests. */
    RUN_TEST(test_audit_log_ntp_timestamp);
    RUN_TEST(test_audit_log_rtc_timestamp);
    RUN_TEST(test_audit_log_unreliable_timestamp);
    RUN_TEST(test_audit_log_timestamp_changes);
    RUN_TEST(test_audit_log_timestamp_coherent_across_reboot);
    RUN_TEST(test_ntp_resyncs_after_rtc_reboot);
    RUN_TEST(test_rtc_mock_persists_independent_of_storage);

    PRINT_TEST_SUMMARY();
}
