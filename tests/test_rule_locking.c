/**
 * @file test_rule_locking.c
 * @brief Tests for SAFETY_LOCKED rule enforcement.
 *
 * Architecture ref: Section 6 (SAFETY_LOCKED rules immutable from field),
 *                   Section 4 (T6: field misconfiguration).
 * Requirement: SR-004 — SAFETY_LOCKED rules shall be unmodifiable
 *             via field config portal.
 *
 * Phase 11: Updated to use per-user RBAC authentication.
 */

#include "test_helpers/test_utils.h"
#include "../firmware/shared/rule_engine_core.h"
#include "../firmware/hub/config_portal.h"
#include <string.h>

/* ---------- Test: rule_class field in struct ---------- */
static int test_rule_class_in_struct(void)
{
    RuleEntry_t rule;
    memset(&rule, 0, sizeof(rule));

    /* Must be able to set and read rule_class. */
    rule.rule_class = RULE_CLASS_SAFETY_LOCKED;
    TEST_ASSERT_EQUAL(RULE_CLASS_SAFETY_LOCKED, rule.rule_class);

    rule.rule_class = RULE_CLASS_OPERATIONAL;
    TEST_ASSERT_EQUAL(RULE_CLASS_OPERATIONAL, rule.rule_class);
    TEST_PASS();
}

/* ---------- Test: rule_class field does not break packing ---------- */
static int test_rule_class_no_struct_growth(void)
{
    /* The struct MUST remain 16 bytes even with rule_class. */
    TEST_ASSERT_EQUAL(16, sizeof(RuleEntry_t));
    TEST_PASS();
}

/* ---------- Test: inline safety-locked check ---------- */
static int test_safety_locked_inline_check(void)
{
    RuleEntry_t locked_rule;
    memset(&locked_rule, 0, sizeof(locked_rule));
    locked_rule.rule_class = RULE_CLASS_SAFETY_LOCKED;
    TEST_ASSERT(rule_is_safety_locked(&locked_rule) == true);

    RuleEntry_t op_rule;
    memset(&op_rule, 0, sizeof(op_rule));
    op_rule.rule_class = RULE_CLASS_OPERATIONAL;
    TEST_ASSERT(rule_is_safety_locked(&op_rule) == false);

    TEST_ASSERT(rule_is_safety_locked(NULL) == false);
    TEST_PASS();
}

/* ---------- Test: config portal rejects safety-locked write (technician) ---------- */
static int test_config_portal_rejects_safety_locked(void)
{
    config_portal_init();
    config_portal_add_user(NULL, "tech1", 5, "pass123", 7, ROLE_TECHNICIAN, 0);

    ConfigSession_t session;
    ConfigPortalResult_t auth_result = config_portal_authenticate("tech1", 5, "pass123", 7, 1000, &session);
    TEST_ASSERT(auth_result == CONFIG_OK);
    TEST_ASSERT(session.role == ROLE_TECHNICIAN);

    /* Try to reject a safety-locked write. */
    ConfigPortalResult_t reject_result = config_portal_reject_safety_locked(
        &session, 1001, 2000);
    TEST_ASSERT(reject_result == CONFIG_ERR_SAFETY_LOCKED);
    TEST_PASS();
}

/* ---------- Test: safety-locked rejection also enforced for admin ---------- */
static int test_config_portal_rejects_safety_locked_admin(void)
{
    config_portal_init();
    config_portal_add_user(NULL, "admin1", 6, "adminpw", 7, ROLE_ADMIN, 0);

    ConfigSession_t session;
    ConfigPortalResult_t auth_result = config_portal_authenticate("admin1", 6, "adminpw", 7, 1000, &session);
    TEST_ASSERT(auth_result == CONFIG_OK);
    TEST_ASSERT(session.role == ROLE_ADMIN);

    /* Admin also cannot modify safety-locked rules. */
    ConfigPortalResult_t reject_result = config_portal_reject_safety_locked(
        &session, 1001, 2000);
    TEST_ASSERT(reject_result == CONFIG_ERR_SAFETY_LOCKED);
    TEST_PASS();
}

/* ---------- Test: config portal audit logs rejection ---------- */
static int test_config_portal_audit_logs_rejection(void)
{
    config_portal_init();
    config_portal_add_user(NULL, "tech1", 5, "pass123", 7, ROLE_TECHNICIAN, 0);

    ConfigSession_t session;
    config_portal_authenticate("tech1", 5, "pass123", 7, 1000, &session);

    /* Reject a safety-locked write. */
    config_portal_reject_safety_locked(&session, 1001, 2000);

    /* Check audit log has the rejection entry. */
    AuditLogEntry_t entries[8];
    uint8_t count = config_portal_get_audit_log(entries, 8);

    bool found_rejection = false;
    for (uint8_t i = 0; i < count; i++) {
        if (entries[i].event_type == AUDIT_SAFETY_LOCKED_REJECTED &&
            entries[i].rule_id == 1001) {
            found_rejection = true;
            break;
        }
    }
    TEST_ASSERT(found_rejection == true);
    TEST_PASS();
}

/* ---------- Test: expired session cannot write ---------- */
static int test_expired_session_cannot_write(void)
{
    config_portal_init();
    config_portal_add_user(NULL, "tech1", 5, "pass123", 7, ROLE_TECHNICIAN, 0);

    ConfigSession_t session;
    config_portal_authenticate("tech1", 5, "pass123", 7, 1000, &session);

    /* Try to write after session expires (15 min + 1 sec). */
    uint64_t expired_time = 1000 + CONFIG_PORTAL_IDLE_TIMEOUT_MS + 1000;
    ConfigWriteRequest_t request = { .rule_id = 2001, .new_threshold = 50.0f };
    ConfigPortalResult_t result = config_portal_write(&session, &request, expired_time);
    TEST_ASSERT(result == CONFIG_ERR_SESSION_EXPIRED);
    TEST_PASS();
}

/* ---------- Main ---------- */
int main(void)
{
    int _total = 0, _passed = 0, _failed = 0;

    printf("=== test_rule_locking ===\n");
    RUN_TEST(test_rule_class_in_struct);
    RUN_TEST(test_rule_class_no_struct_growth);
    RUN_TEST(test_safety_locked_inline_check);
    RUN_TEST(test_config_portal_rejects_safety_locked);
    RUN_TEST(test_config_portal_rejects_safety_locked_admin);
    RUN_TEST(test_config_portal_audit_logs_rejection);
    RUN_TEST(test_expired_session_cannot_write);

    PRINT_TEST_SUMMARY();
}
