/**
 * @file test_audit_log_persistence.c
 * @brief Tests for audit log persistence across simulated reboots.
 *
 * FIX 3 (Phase 1.1): Audit log entries must survive a reboot.
 * Uses a RAM-backed storage backend to simulate NVS/LittleFS.
 *
 * Phase 11: Updated to use per-user RBAC authentication.
 * Note: config_portal_add_user() generates an audit entry (unlike the
 * old config_portal_set_pin() which did not). Reboot simulations must
 * account for this extra entry when comparing counts.
 */

#include "test_helpers/test_utils.h"
#include "../firmware/shared/audit_log.h"
#include "../firmware/hub/config_portal.h"
#include <string.h>

/* ---------- RAM-backed storage for testing ---------- */

#define PERSISTENT_LOG_SIZE  32

static AuditLogEntry_t s_persist_log[PERSISTENT_LOG_SIZE];
static uint8_t s_persist_write_index = 0;
static uint32_t s_persist_auth_failure_count = 0;
static bool s_persist_dirty = false;

static bool test_storage_save(const AuditLogEntry_t *entries, uint8_t count,
                               uint8_t write_index, uint32_t auth_failure_count)
{
    (void)count;
    memcpy(s_persist_log, entries, sizeof(AuditLogEntry_t) * PERSISTENT_LOG_SIZE);
    s_persist_write_index = write_index;
    s_persist_auth_failure_count = auth_failure_count;
    s_persist_dirty = true;
    return true;
}

static bool test_storage_load(AuditLogEntry_t *entries, uint8_t max_entries,
                               uint8_t *write_index, uint32_t *auth_failure_count)
{
    (void)max_entries;
    memcpy(entries, s_persist_log, sizeof(AuditLogEntry_t) * PERSISTENT_LOG_SIZE);
    *write_index = s_persist_write_index;
    *auth_failure_count = s_persist_auth_failure_count;
    return true;
}

static const AuditLogStorage_t s_test_storage = {
    .save = test_storage_save,
    .load = test_storage_load
};

/* ---------- Helpers ---------- */

/**
 * Simulate a reboot: re-register storage, re-init portal (which calls
 * audit_log_init to reload from storage), then re-create the test user
 * (which generates one extra audit entry — "User account added").
 */
static void simulate_reboot(void)
{
    audit_log_set_storage(&s_test_storage);
    config_portal_init();
    config_portal_add_user(NULL, "user1", 5, "1234", 4, ROLE_TECHNICIAN, 0);
}

/* ---------- Test: basic persistence across reboot ---------- */
static int test_audit_log_survives_reboot(void)
{
    /* Setup: register storage and init. */
    memset(s_persist_log, 0, sizeof(s_persist_log));
    s_persist_write_index = 0;
    s_persist_auth_failure_count = 0;
    s_persist_dirty = false;
    audit_log_set_storage(&s_test_storage);
    config_portal_init();
    config_portal_add_user(NULL, "user1", 5, "1234", 4, ROLE_TECHNICIAN, 0);

    /* Generate some audit events. */
    ConfigSession_t session;
    config_portal_authenticate("user1", 5, "1234", 4, 1000, &session);
    config_portal_authenticate("user1", 5, "wrong", 5, 2000, &session);  /* Wrong credential. */
    config_portal_reject_safety_locked(&session, 1001, 3000);

    /* Verify entries exist before reboot. */
    AuditLogEntry_t entries_before[16];
    uint8_t count_before = config_portal_get_audit_log(entries_before, 16);
    TEST_ASSERT(count_before >= 3);
    TEST_ASSERT(s_persist_dirty == true);

    /* Record the first N entries for comparison after reboot. */
    AuditLogEntry_t saved_entries[16];
    memcpy(saved_entries, entries_before, sizeof(AuditLogEntry_t) * count_before);

    /* Simulate reboot — wipe ALL in-RAM state, reinit from persistence. */
    simulate_reboot();

    /* After reboot + add_user, we have count_before persisted entries
     * PLUS 1 new "User account added" entry from simulate_reboot's add_user. */
    AuditLogEntry_t entries_after[16];
    uint8_t count_after = config_portal_get_audit_log(entries_after, 16);
    TEST_ASSERT(count_after >= count_before);

    /* The persisted entries (indices 0..count_before-1) should be intact.
     * The new "User account added" entry from simulate_reboot is at the end. */
    for (uint8_t i = 0; i < count_before; i++) {
        TEST_ASSERT_EQUAL(saved_entries[i].event_type, entries_after[i].event_type);
        TEST_ASSERT_EQUAL(saved_entries[i].session_id, entries_after[i].session_id);
        TEST_ASSERT_EQUAL(saved_entries[i].rule_id, entries_after[i].rule_id);
    }
    TEST_PASS();
}

/* ---------- Test: auth failure count persists ---------- */
static int test_auth_failure_count_persists(void)
{
    memset(s_persist_log, 0, sizeof(s_persist_log));
    s_persist_write_index = 0;
    s_persist_auth_failure_count = 0;
    s_persist_dirty = false;
    audit_log_set_storage(&s_test_storage);
    config_portal_init();
    config_portal_add_user(NULL, "user1", 5, "1234", 4, ROLE_TECHNICIAN, 0);

    /* Generate auth failures — try wrong credentials. */
    ConfigSession_t session;
    config_portal_authenticate("user1", 5, "wrong", 5, 1000, &session);
    config_portal_authenticate("user1", 5, "bad", 3, 2000, &session);
    config_portal_authenticate("user1", 5, "nope", 4, 3000, &session);

    uint32_t failures_before = config_portal_get_auth_failure_count();
    TEST_ASSERT(failures_before >= 3);

    /* Simulate reboot. */
    simulate_reboot();

    /* Auth failure count should be restored. */
    TEST_ASSERT_EQUAL(failures_before, config_portal_get_auth_failure_count());
    TEST_PASS();
}

/* ---------- Test: persistence disabled when no storage ---------- */
static int test_no_storage_no_persistence(void)
{
    audit_log_set_storage(NULL);
    config_portal_init();
    config_portal_add_user(NULL, "user1", 5, "1234", 4, ROLE_TECHNICIAN, 0);

    ConfigSession_t session;
    config_portal_authenticate("user1", 5, "1234", 4, 1000, &session);

    /* Without storage, no persistence happens (but no crash either). */
    AuditLogEntry_t entries[8];
    uint8_t count = config_portal_get_audit_log(entries, 8);
    TEST_ASSERT(count >= 1);

    /* Reboot without storage — entries are lost (expected). */
    audit_log_set_storage(NULL);
    config_portal_init();
    config_portal_add_user(NULL, "user1", 5, "1234", 4, ROLE_TECHNICIAN, 0);
    AuditLogEntry_t entries2[8];
    uint8_t count2 = config_portal_get_audit_log(entries2, 8);
    /* Only the new "User account added" entry exists — old entries lost. */
    TEST_ASSERT(count2 >= 1);
    TEST_ASSERT(count2 < count);
    TEST_PASS();
}

/* ---------- Test: multiple reboots accumulate ---------- */
static int test_multiple_reboots(void)
{
    memset(s_persist_log, 0, sizeof(s_persist_log));
    s_persist_write_index = 0;
    s_persist_auth_failure_count = 0;
    audit_log_set_storage(&s_test_storage);
    config_portal_init();
    config_portal_add_user(NULL, "user1", 5, "1234", 4, ROLE_TECHNICIAN, 0);

    /* First boot: generate events. */
    ConfigSession_t session;
    config_portal_authenticate("user1", 5, "1234", 4, 1000, &session);

    AuditLogEntry_t entries1[8];
    uint8_t count1 = config_portal_get_audit_log(entries1, 8);

    /* Reboot. */
    simulate_reboot();

    /* Second boot: generate more events. */
    config_portal_authenticate("user1", 5, "1234", 4, 2000, &session);
    config_portal_authenticate("user1", 5, "wrong", 5, 3000, &session);  /* Failure. */

    AuditLogEntry_t entries2[16];
    uint8_t count2 = config_portal_get_audit_log(entries2, 16);
    TEST_ASSERT(count2 > count1);  /* More entries than first boot. */

    /* Reboot again. */
    simulate_reboot();

    /* Third boot: all entries from second boot should survive.
     * Note: simulate_reboot adds one "User account added" entry,
     * so count3 >= count2. The key assertion is that the original
     * entries are preserved (count grew, not shrank). */
    AuditLogEntry_t entries3[16];
    uint8_t count3 = config_portal_get_audit_log(entries3, 16);
    TEST_ASSERT(count3 >= count2);
    TEST_PASS();
}

/* ---------- Main ---------- */
int main(void)
{
    int _total = 0, _passed = 0, _failed = 0;

    printf("=== test_audit_log_persistence ===\n");
    RUN_TEST(test_audit_log_survives_reboot);
    RUN_TEST(test_auth_failure_count_persists);
    RUN_TEST(test_no_storage_no_persistence);
    RUN_TEST(test_multiple_reboots);

    PRINT_TEST_SUMMARY();
}
