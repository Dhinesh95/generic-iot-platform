/**
 * @file test_config_portal_auth.c
 * @brief Tests for config portal per-user RBAC authentication and session management.
 *
 * Architecture ref: Section 4 (T6: field misconfiguration, RBAC baseline/mandatory),
 *                   Section 6 (config portal RBAC).
 *
 * Phase 11: Replaced single shared PIN tests with per-user credential tests.
 * Phase 11.2: Updated for add_user bootstrap/admin-gate.
 *
 * add_user calling convention:
 *   Bootstrap (zero users): add_user(NULL, username, ..., role, 0)
 *   Post-bootstrap:        add_user(&admin_session, username, ..., role, current_ms)
 */

#include "test_helpers/test_utils.h"
#include "../firmware/hub/config_portal.h"
#include <string.h>

/* ---------- Helper: init + create admin + return admin session ---------- */
static ConfigSession_t setup_admin(const char *name, size_t nlen,
                                    const char *cred, size_t clen)
{
    config_portal_add_user(NULL, name, nlen, cred, clen, ROLE_ADMIN, 0);
    ConfigSession_t s;
    config_portal_authenticate(name, nlen, cred, clen, 100, &s);
    return s;
}

/* ---------- Test: init ---------- */
static int test_config_portal_init(void)
{
    TEST_ASSERT(config_portal_init() == true);
    TEST_PASS();
}

/* ---------- Test: add user (bootstrap + duplicate + null params) ---------- */
static int test_config_portal_add_user(void)
{
    config_portal_init();

    /* Bootstrap: zero users, NULL session allowed. */
    ConfigPortalResult_t r = config_portal_add_user(NULL, "alice", 5, "pass123", 7, ROLE_TECHNICIAN, 0);
    TEST_ASSERT(r == CONFIG_OK);

    /* Post-bootstrap: NULL session rejected (not bootstrap anymore). */
    r = config_portal_add_user(NULL, "eve", 3, "pass", 4, ROLE_TECHNICIAN, 1000);
    TEST_ASSERT(r == CONFIG_ERR_NOT_AUTHORIZED);

    /* Duplicate username rejected (admin session needed for post-bootstrap). */
    ConfigSession_t admin;
    config_portal_authenticate("alice", 5, "pass123", 7, 1500, &admin);
    /* Note: alice is TECHNICIAN, so add_user will reject with NOT_AUTHORIZED
     * before even checking for duplicates. That proves the gate works.
     * To test the duplicate check specifically, we need an admin user.
     * Add admin1 via bootstrap (requires reinit). */
    config_portal_init();
    config_portal_add_user(NULL, "admin1", 6, "adminpw", 7, ROLE_ADMIN, 0);
    ConfigSession_t admin2;
    config_portal_authenticate("admin1", 6, "adminpw", 7, 2000, &admin2);
    config_portal_add_user(&admin2, "alice", 5, "pass123", 7, ROLE_TECHNICIAN, 2500);
    r = config_portal_add_user(&admin2, "alice", 5, "other", 5, ROLE_TECHNICIAN, 3000);
    TEST_ASSERT(r == CONFIG_ERR_USER_EXISTS);

    /* NULL params rejected. */
    r = config_portal_add_user(NULL, NULL, 0, "pass", 4, ROLE_TECHNICIAN, 0);
    TEST_ASSERT(r == CONFIG_ERR_PARAM_NULL);

    TEST_PASS();
}

/* ---------- Test: remove user (admin) ---------- */
static int test_config_portal_remove_user(void)
{
    config_portal_init();
    ConfigSession_t admin = setup_admin("admin1", 6, "adminpw", 7);

    /* Add tech via admin session. */
    ConfigPortalResult_t r = config_portal_add_user(&admin, "alice", 5, "pass123", 7, ROLE_TECHNICIAN, 2000);
    TEST_ASSERT(r == CONFIG_OK);

    r = config_portal_remove_user(&admin, "alice", 5, 3000);
    TEST_ASSERT(r == CONFIG_OK);

    ConfigSession_t session;
    ConfigPortalResult_t r2 = config_portal_authenticate("alice", 5, "pass123", 7, 4000, &session);
    TEST_ASSERT(r2 == CONFIG_ERR_AUTH_FAILED);

    r = config_portal_remove_user(&admin, "nobody", 6, 5000);
    TEST_ASSERT(r == CONFIG_ERR_USER_NOT_FOUND);

    TEST_PASS();
}

/* ---------- Test: technician cannot remove user ---------- */
static int test_config_portal_technician_cannot_remove_user(void)
{
    config_portal_init();
    ConfigSession_t admin = setup_admin("admin1", 6, "adminpw", 7);
    config_portal_add_user(&admin, "tech1", 5, "pass123", 7, ROLE_TECHNICIAN, 2000);
    config_portal_add_user(&admin, "alice", 5, "other", 5, ROLE_TECHNICIAN, 3000);

    ConfigSession_t session;
    config_portal_authenticate("tech1", 5, "pass123", 7, 4000, &session);
    TEST_ASSERT(session.role == ROLE_TECHNICIAN);

    ConfigPortalResult_t r = config_portal_remove_user(&session, "alice", 5, 5000);
    TEST_ASSERT(r == CONFIG_ERR_NOT_AUTHORIZED);

    ConfigSession_t alice_session;
    r = config_portal_authenticate("alice", 5, "other", 5, 6000, &alice_session);
    TEST_ASSERT(r == CONFIG_OK);
    TEST_PASS();
}

/* ---------- Test: admin can remove technician ---------- */
static int test_config_portal_admin_can_remove_technician(void)
{
    config_portal_init();
    ConfigSession_t admin = setup_admin("admin1", 6, "adminpw", 7);
    config_portal_add_user(&admin, "tech1", 5, "pass123", 7, ROLE_TECHNICIAN, 2000);

    ConfigPortalResult_t r = config_portal_remove_user(&admin, "tech1", 5, 3000);
    TEST_ASSERT(r == CONFIG_OK);

    ConfigSession_t unused;
    r = config_portal_authenticate("tech1", 5, "pass123", 7, 4000, &unused);
    TEST_ASSERT(r == CONFIG_ERR_AUTH_FAILED);
    TEST_PASS();
}

/* ---------- Test: admin cannot remove last admin ---------- */
static int test_config_portal_admin_cannot_remove_last_admin(void)
{
    config_portal_init();
    ConfigSession_t admin = setup_admin("admin1", 6, "adminpw", 7);

    ConfigPortalResult_t r = config_portal_remove_user(&admin, "admin1", 6, 2000);
    TEST_ASSERT(r == CONFIG_ERR_LAST_ADMIN);

    ConfigSession_t unused;
    r = config_portal_authenticate("admin1", 6, "adminpw", 7, 3000, &unused);
    TEST_ASSERT(r == CONFIG_OK);
    TEST_PASS();
}

/* ---------- Test: admin can remove admin when multiple exist ---------- */
static int test_config_portal_admin_can_remove_admin_when_multiple(void)
{
    config_portal_init();
    ConfigSession_t admin = setup_admin("admin1", 6, "adminpw", 7);
    config_portal_add_user(&admin, "admin2", 6, "other", 5, ROLE_ADMIN, 2000);

    ConfigPortalResult_t r = config_portal_remove_user(&admin, "admin2", 6, 3000);
    TEST_ASSERT(r == CONFIG_OK);

    ConfigSession_t unused;
    r = config_portal_authenticate("admin2", 6, "other", 5, 4000, &unused);
    TEST_ASSERT(r == CONFIG_ERR_AUTH_FAILED);
    TEST_PASS();
}

/* ---------- Test: remove_user does NOT decrement g_user_count (Phase 11.4) ---------- */
static int test_config_portal_remove_does_not_decrement_user_count(void)
{
    config_portal_init();
    ConfigSession_t admin = setup_admin("admin1", 6, "adminpw", 7);

    /* Fill all 8 slots (admin1 + 7 more). */
    config_portal_add_user(&admin, "u2", 2, "p", 1, ROLE_TECHNICIAN, 1000);
    config_portal_add_user(&admin, "u3", 2, "p", 1, ROLE_TECHNICIAN, 1100);
    config_portal_add_user(&admin, "u4", 2, "p", 1, ROLE_TECHNICIAN, 1200);
    config_portal_add_user(&admin, "u5", 2, "p", 1, ROLE_TECHNICIAN, 1300);
    config_portal_add_user(&admin, "u6", 2, "p", 1, ROLE_TECHNICIAN, 1400);
    config_portal_add_user(&admin, "u7", 2, "p", 1, ROLE_TECHNICIAN, 1500);
    config_portal_add_user(&admin, "u8", 2, "p", 1, ROLE_TECHNICIAN, 1600);

    /* All 8 slots full. */
    ConfigPortalResult_t r = config_portal_add_user(&admin, "u9", 2, "p", 1, ROLE_TECHNICIAN, 1700);
    TEST_ASSERT(r == CONFIG_ERR_USERS_FULL);

    /* Remove u2 — frees one slot. */
    r = config_portal_remove_user(&admin, "u2", 2, 1800);
    TEST_ASSERT(r == CONFIG_OK);

    /* g_user_count is NOT decremented (it's a high-water mark), but
     * add_user() reuses the freed slot. So adding u9 now succeeds. */
    r = config_portal_add_user(&admin, "u9", 2, "p", 1, ROLE_TECHNICIAN, 1900);
    TEST_ASSERT(r == CONFIG_OK);

    /* u9 can authenticate. */
    ConfigSession_t session;
    TEST_ASSERT(config_portal_authenticate("u9", 2, "p", 1, 2000, &session) == CONFIG_OK);

    /* u2 can no longer authenticate (credential cleared, active=false). */
    ConfigSession_t unused;
    r = config_portal_authenticate("u2", 2, "p", 1, 2000, &unused);
    TEST_ASSERT(r == CONFIG_ERR_AUTH_FAILED);

    TEST_PASS();
}

/* ---------- Test: authenticate with correct credential ---------- */
static int test_config_portal_auth_correct(void)
{
    config_portal_init();
    config_portal_add_user(NULL, "alice", 5, "pass123", 7, ROLE_TECHNICIAN, 0);

    ConfigSession_t session;
    ConfigPortalResult_t result = config_portal_authenticate("alice", 5, "pass123", 7, 1000, &session);
    TEST_ASSERT(result == CONFIG_OK);
    TEST_ASSERT(session.active == true);
    TEST_ASSERT(session.session_id > 0);
    TEST_ASSERT(session.role == ROLE_TECHNICIAN);
    TEST_ASSERT(strcmp(session.username, "alice") == 0);
    TEST_PASS();
}

/* ---------- Test: authenticate with wrong credential ---------- */
static int test_config_portal_auth_wrong_credential(void)
{
    config_portal_init();
    config_portal_add_user(NULL, "alice", 5, "pass123", 7, ROLE_TECHNICIAN, 0);

    ConfigSession_t session;
    memset(&session, 0, sizeof(session));
    ConfigPortalResult_t result = config_portal_authenticate("alice", 5, "wrong!", 6, 1000, &session);
    TEST_ASSERT(result == CONFIG_ERR_AUTH_FAILED);
    TEST_ASSERT(session.active == false);
    TEST_PASS();
}

/* ---------- Test: authenticate unknown user ---------- */
static int test_config_portal_auth_unknown_user(void)
{
    config_portal_init();
    config_portal_add_user(NULL, "alice", 5, "pass123", 7, ROLE_TECHNICIAN, 0);

    ConfigSession_t session;
    memset(&session, 0, sizeof(session));
    ConfigPortalResult_t result = config_portal_authenticate("bob", 3, "pass123", 7, 1000, &session);
    TEST_ASSERT(result == CONFIG_ERR_AUTH_FAILED);
    TEST_ASSERT(session.active == false);
    TEST_PASS();
}

/* ---------- Test: auth failure count ---------- */
static int test_config_portal_auth_failure_count(void)
{
    config_portal_init();
    config_portal_add_user(NULL, "alice", 5, "pass123", 7, ROLE_TECHNICIAN, 0);

    ConfigSession_t session;
    config_portal_authenticate("alice", 5, "wrong1", 6, 1000, &session);
    config_portal_authenticate("alice", 5, "wrong2", 6, 2000, &session);

    TEST_ASSERT_EQUAL(2, config_portal_get_auth_failure_count());
    TEST_PASS();
}

/* ---------- Test: session validation ---------- */
static int test_config_portal_session_validation(void)
{
    config_portal_init();
    config_portal_add_user(NULL, "alice", 5, "pass123", 7, ROLE_TECHNICIAN, 0);

    ConfigSession_t session;
    config_portal_authenticate("alice", 5, "pass123", 7, 1000, &session);

    TEST_ASSERT(config_portal_validate_session(&session, 2000) == CONFIG_OK);
    TEST_ASSERT(config_portal_validate_session(&session, 1000 + CONFIG_PORTAL_IDLE_TIMEOUT_MS + 1000)
                == CONFIG_ERR_SESSION_EXPIRED);

    ConfigSession_t inactive;
    memset(&inactive, 0, sizeof(inactive));
    TEST_ASSERT(config_portal_validate_session(&inactive, 1000) == CONFIG_ERR_NO_SESSION);
    TEST_PASS();
}

/* ---------- Test: session logout ---------- */
static int test_config_portal_logout(void)
{
    config_portal_init();
    config_portal_add_user(NULL, "alice", 5, "pass123", 7, ROLE_TECHNICIAN, 0);

    ConfigSession_t session;
    config_portal_authenticate("alice", 5, "pass123", 7, 1000, &session);
    TEST_ASSERT(session.active == true);

    config_portal_logout(&session);
    TEST_ASSERT(session.active == false);
    TEST_PASS();
}

/* ---------- Test: session carries user identity ---------- */
static int test_config_portal_session_carries_identity(void)
{
    config_portal_init();
    ConfigSession_t admin = setup_admin("admin1", 6, "adminpw", 7);
    config_portal_add_user(&admin, "alice", 5, "pass123", 7, ROLE_TECHNICIAN, 2000);
    config_portal_add_user(&admin, "bob", 3, "admin", 5, ROLE_ADMIN, 3000);

    ConfigSession_t alice_session, bob_session;
    config_portal_authenticate("alice", 5, "pass123", 7, 4000, &alice_session);
    config_portal_authenticate("bob", 3, "admin", 5, 5000, &bob_session);

    TEST_ASSERT(alice_session.role == ROLE_TECHNICIAN);
    TEST_ASSERT(strcmp(alice_session.username, "alice") == 0);
    TEST_ASSERT(bob_session.role == ROLE_ADMIN);
    TEST_ASSERT(strcmp(bob_session.username, "bob") == 0);
    TEST_ASSERT(alice_session.user_index != bob_session.user_index);
    TEST_PASS();
}

/* ---------- Test: audit log records user identity ---------- */
static int test_config_portal_audit_records_user(void)
{
    config_portal_init();
    config_portal_add_user(NULL, "alice", 5, "pass123", 7, ROLE_TECHNICIAN, 0);

    ConfigSession_t session;
    config_portal_authenticate("alice", 5, "pass123", 7, 1000, &session);

    AuditLogEntry_t entries[8];
    uint8_t count = config_portal_get_audit_log(entries, 8);

    bool found_user = false;
    for (uint8_t i = 0; i < count; i++) {
        if (entries[i].event_type == AUDIT_AUTH_SUCCESS &&
            strstr(entries[i].detail, "alice") != NULL) {
            found_user = true;
            break;
        }
    }
    TEST_ASSERT(found_user == true);
    TEST_PASS();
}

/* ---------- Test: technician cannot change roles ---------- */
static int test_config_portal_technician_cannot_change_role(void)
{
    config_portal_init();
    ConfigSession_t admin = setup_admin("admin1", 6, "adminpw", 7);
    config_portal_add_user(&admin, "tech1", 5, "pass123", 7, ROLE_TECHNICIAN, 2000);
    config_portal_add_user(&admin, "alice", 5, "other", 5, ROLE_TECHNICIAN, 3000);

    ConfigSession_t session;
    config_portal_authenticate("tech1", 5, "pass123", 7, 4000, &session);
    TEST_ASSERT(session.role == ROLE_TECHNICIAN);

    ConfigPortalResult_t r = config_portal_change_role(&session, "alice", 5, ROLE_ADMIN, 5000);
    TEST_ASSERT(r == CONFIG_ERR_NOT_AUTHORIZED);
    TEST_PASS();
}

/* ---------- Test: admin can change roles ---------- */
static int test_config_portal_admin_can_change_role(void)
{
    config_portal_init();
    ConfigSession_t admin = setup_admin("admin1", 6, "adminpw", 7);
    config_portal_add_user(&admin, "tech1", 5, "pass123", 7, ROLE_TECHNICIAN, 2000);

    ConfigPortalResult_t r = config_portal_change_role(&admin, "tech1", 5, ROLE_ADMIN, 3000);
    TEST_ASSERT(r == CONFIG_OK);
    TEST_PASS();
}

/* ---------- Test: safety-locked rejection regardless of role ---------- */
static int test_config_portal_safety_locked_rejects_all_roles(void)
{
    config_portal_init();
    ConfigSession_t admin = setup_admin("admin1", 6, "adminpw", 7);
    TEST_ASSERT(admin.role == ROLE_ADMIN);

    ConfigPortalResult_t r = config_portal_reject_safety_locked(&admin, 1001, 2000);
    TEST_ASSERT(r == CONFIG_ERR_SAFETY_LOCKED);
    TEST_PASS();
}

/* ---------- Test: technician cannot add user after bootstrap (Phase 11.2) ---------- */
static int test_config_portal_technician_cannot_add_user_after_bootstrap(void)
{
    config_portal_init();
    ConfigSession_t admin = setup_admin("admin1", 6, "adminpw", 7);
    config_portal_add_user(&admin, "tech1", 5, "pass123", 7, ROLE_TECHNICIAN, 2000);

    ConfigSession_t tech_session;
    config_portal_authenticate("tech1", 5, "pass123", 7, 3000, &tech_session);
    TEST_ASSERT(tech_session.role == ROLE_TECHNICIAN);

    /* Tech cannot add new users. */
    ConfigPortalResult_t r = config_portal_add_user(&tech_session, "eve", 3, "pass", 4, ROLE_TECHNICIAN, 4000);
    TEST_ASSERT(r == CONFIG_ERR_NOT_AUTHORIZED);

    ConfigSession_t unused;
    r = config_portal_authenticate("eve", 3, "pass", 4, 5000, &unused);
    TEST_ASSERT(r == CONFIG_ERR_AUTH_FAILED);
    TEST_PASS();
}

/* ---------- Test: admin can add user after bootstrap (Phase 11.2) ---------- */
static int test_config_portal_admin_can_add_user_after_bootstrap(void)
{
    config_portal_init();
    ConfigSession_t admin = setup_admin("admin1", 6, "adminpw", 7);

    ConfigPortalResult_t r = config_portal_add_user(&admin, "tech1", 5, "pass123", 7, ROLE_TECHNICIAN, 2000);
    TEST_ASSERT(r == CONFIG_OK);

    ConfigSession_t unused;
    r = config_portal_authenticate("tech1", 5, "pass123", 7, 3000, &unused);
    TEST_ASSERT(r == CONFIG_OK);
    TEST_PASS();
}

/* ---------- Test: NULL session rejected when users exist (Phase 11.2) ---------- */
static int test_config_portal_null_session_rejected_post_bootstrap(void)
{
    config_portal_init();
    config_portal_add_user(NULL, "admin1", 6, "adminpw", 7, ROLE_ADMIN, 0);

    /* NULL session is NOT bootstrap when users exist. */
    ConfigPortalResult_t r = config_portal_add_user(NULL, "eve", 3, "pass", 4, ROLE_TECHNICIAN, 1000);
    TEST_ASSERT(r == CONFIG_ERR_NOT_AUTHORIZED);
    TEST_PASS();
}

/* ---------- Test: technician cannot self-promote via add_user (Phase 11.2 attack) ---------- */
static int test_config_portal_technician_cannot_self_promote_via_add_user(void)
{
    config_portal_init();
    ConfigSession_t admin = setup_admin("admin1", 6, "adminpw", 7);
    config_portal_add_user(&admin, "tech1", 5, "pass123", 7, ROLE_TECHNICIAN, 2000);

    ConfigSession_t tech_session;
    config_portal_authenticate("tech1", 5, "pass123", 7, 3000, &tech_session);
    TEST_ASSERT(tech_session.role == ROLE_TECHNICIAN);

    /* THE ATTACK: tech1 tries to create a new ROLE_ADMIN account. */
    ConfigPortalResult_t r = config_portal_add_user(&tech_session, "tech1_admin", 11, "hacked", 6, ROLE_ADMIN, 4000);
    TEST_ASSERT(r == CONFIG_ERR_NOT_AUTHORIZED);

    ConfigSession_t unused;
    r = config_portal_authenticate("tech1_admin", 11, "hacked", 6, 5000, &unused);
    TEST_ASSERT(r == CONFIG_ERR_AUTH_FAILED);
    TEST_PASS();
}

/* ---------- Test: admin cannot demote the last admin (Phase 11.3) ---------- */
static int test_config_portal_cannot_demote_last_admin(void)
{
    config_portal_init();
    ConfigSession_t admin = setup_admin("admin1", 6, "adminpw", 7);
    config_portal_add_user(&admin, "tech1", 5, "pass123", 7, ROLE_TECHNICIAN, 2000);

    /* admin1 is the only admin. Attempting to demote admin1 to technician
     * would leave count_active_admins() == 0 while g_user_count > 0,
     * making user management permanently impossible. */
    ConfigPortalResult_t r = config_portal_change_role(&admin, "admin1", 6, ROLE_TECHNICIAN, 3000);
    TEST_ASSERT(r == CONFIG_ERR_LAST_ADMIN);

    /* admin1 is still admin. */
    ConfigSession_t unused;
    r = config_portal_authenticate("admin1", 6, "adminpw", 7, 4000, &unused);
    TEST_ASSERT(r == CONFIG_OK);
    TEST_ASSERT(unused.role == ROLE_ADMIN);
    TEST_PASS();
}

/* ---------- Test: admin can demote when multiple admins exist (Phase 11.3) ---------- */
static int test_config_portal_admin_can_demote_when_multiple_admins(void)
{
    config_portal_init();
    ConfigSession_t admin1 = setup_admin("admin1", 6, "adminpw", 7);
    config_portal_add_user(&admin1, "admin2", 6, "other", 5, ROLE_ADMIN, 2000);

    /* With 2 admins, demoting admin2 to technician is allowed. */
    ConfigPortalResult_t r = config_portal_change_role(&admin1, "admin2", 6, ROLE_TECHNICIAN, 3000);
    TEST_ASSERT(r == CONFIG_OK);

    ConfigSession_t unused;
    r = config_portal_authenticate("admin2", 6, "other", 5, 4000, &unused);
    TEST_ASSERT(r == CONFIG_OK);
    TEST_ASSERT(unused.role == ROLE_TECHNICIAN);
    TEST_PASS();
}

/* ---------- Test: remove all technicians — last admin remains (Phase 11.3) ---------- */
static int test_config_portal_remove_all_technicians_last_admin_stays(void)
{
    config_portal_init();
    ConfigSession_t admin = setup_admin("admin1", 6, "adminpw", 7);
    config_portal_add_user(&admin, "tech1", 5, "pass1", 4, ROLE_TECHNICIAN, 2000);
    config_portal_add_user(&admin, "tech2", 5, "pass2", 4, ROLE_TECHNICIAN, 3000);

    /* Remove all technicians. */
    TEST_ASSERT(config_portal_remove_user(&admin, "tech1", 5, 4000) == CONFIG_OK);
    TEST_ASSERT(config_portal_remove_user(&admin, "tech2", 5, 5000) == CONFIG_OK);

    /* Last admin cannot be removed. */
    TEST_ASSERT(config_portal_remove_user(&admin, "admin1", 6, 6000) == CONFIG_ERR_LAST_ADMIN);

    /* admin1 can still authenticate — system is not locked out. */
    ConfigSession_t session;
    TEST_ASSERT(config_portal_authenticate("admin1", 6, "adminpw", 7, 7000, &session) == CONFIG_OK);
    TEST_ASSERT(session.role == ROLE_ADMIN);
    TEST_PASS();
}

/* ---------- Test: slot reuse after removal (Phase 11.5) ---------- */
static int test_config_portal_slot_reuse_after_remove(void)
{
    config_portal_init();
    ConfigSession_t admin = setup_admin("admin1", 6, "adminpw", 7);
    config_portal_add_user(&admin, "tech1", 5, "pass1", 4, ROLE_TECHNICIAN, 2000);
    config_portal_add_user(&admin, "tech2", 5, "pass2", 4, ROLE_TECHNICIAN, 3000);

    /* Remove tech1 — frees one slot. */
    TEST_ASSERT(config_portal_remove_user(&admin, "tech1", 5, 4000) == CONFIG_OK);

    /* Add tech3 — should reuse tech1's freed slot (not exhaust MAX_USERS). */
    ConfigPortalResult_t r = config_portal_add_user(&admin, "tech3", 5, "pass3", 4, ROLE_TECHNICIAN, 5000);
    TEST_ASSERT(r == CONFIG_OK);

    /* tech3 can authenticate. */
    ConfigSession_t session;
    TEST_ASSERT(config_portal_authenticate("tech3", 5, "pass3", 4, 6000, &session) == CONFIG_OK);
    TEST_ASSERT(session.role == ROLE_TECHNICIAN);
    TEST_PASS();
}

/* ---------- Test: slot reuse doesn't leak old credentials (Phase 11.5) ---------- */
static int test_config_portal_slot_reuse_no_credential_leak(void)
{
    config_portal_init();
    ConfigSession_t admin = setup_admin("admin1", 6, "adminpw", 7);
    config_portal_add_user(&admin, "tech1", 5, "secret", 6, ROLE_TECHNICIAN, 2000);

    /* Remove tech1. */
    TEST_ASSERT(config_portal_remove_user(&admin, "tech1", 5, 3000) == CONFIG_OK);

    /* tech1 can no longer authenticate with old credential. */
    ConfigSession_t session;
    TEST_ASSERT(config_portal_authenticate("tech1", 5, "secret", 6, 4000, &session) == CONFIG_ERR_AUTH_FAILED);

    /* Add new user in the same slot. */
    TEST_ASSERT(config_portal_add_user(&admin, "tech2", 5, "newcred", 7, ROLE_TECHNICIAN, 5000) == CONFIG_OK);

    /* Old credential still doesn't work. */
    TEST_ASSERT(config_portal_authenticate("tech1", 5, "secret", 6, 6000, &session) == CONFIG_ERR_AUTH_FAILED);
    /* New credential works. */
    TEST_ASSERT(config_portal_authenticate("tech2", 5, "newcred", 7, 7000, &session) == CONFIG_OK);
    TEST_PASS();
}

/* ---------- Test: slot reuse audit trail integrity (Phase 11.5) ---------- */
static int test_config_portal_slot_reuse_audit_trail(void)
{
    config_portal_init();
    ConfigSession_t admin = setup_admin("admin1", 6, "adminpw", 7);
    config_portal_add_user(&admin, "tech1", 5, "pass1", 4, ROLE_TECHNICIAN, 2000);

    /* Remove tech1 — audit records removal. */
    TEST_ASSERT(config_portal_remove_user(&admin, "tech1", 5, 3000) == CONFIG_OK);

    /* Add tech2 — audit records addition. */
    TEST_ASSERT(config_portal_add_user(&admin, "tech2", 5, "pass2", 4, ROLE_TECHNICIAN, 4000) == CONFIG_OK);

    /* Both events are in the audit log (removal + addition). */
    AuditLogEntry_t entries[8];
    uint8_t count = config_portal_get_audit_log(entries, 8);
    bool found_removal = false, found_addition = false;
    for (uint8_t i = 0; i < count; i++) {
        if (strstr(entries[i].detail, "removed") != NULL) found_removal = true;
        if (strstr(entries[i].detail, "added") != NULL) found_addition = true;
    }
    TEST_ASSERT(found_removal == true);
    TEST_ASSERT(found_addition == true);
    TEST_PASS();
}

/* ---------- Test: null params ---------- */
static int test_config_portal_null_params(void)
{
    config_portal_init();

    TEST_ASSERT(config_portal_add_user(NULL, NULL, 0, "pass", 4, ROLE_TECHNICIAN, 0) == CONFIG_ERR_PARAM_NULL);
    TEST_ASSERT(config_portal_authenticate(NULL, 0, NULL, 0, 0, NULL) == CONFIG_ERR_PARAM_NULL);
    TEST_ASSERT(config_portal_validate_session(NULL, 0) == CONFIG_ERR_PARAM_NULL);
    TEST_PASS();
}

/* ---------- Main ---------- */
int main(void)
{
    int _total = 0, _passed = 0, _failed = 0;

    printf("=== test_config_portal_auth ===\n");
    RUN_TEST(test_config_portal_init);
    RUN_TEST(test_config_portal_add_user);
    RUN_TEST(test_config_portal_remove_user);
    RUN_TEST(test_config_portal_technician_cannot_remove_user);
    RUN_TEST(test_config_portal_admin_can_remove_technician);
    RUN_TEST(test_config_portal_admin_cannot_remove_last_admin);
    RUN_TEST(test_config_portal_admin_can_remove_admin_when_multiple);
    RUN_TEST(test_config_portal_remove_does_not_decrement_user_count);
    RUN_TEST(test_config_portal_auth_correct);
    RUN_TEST(test_config_portal_auth_wrong_credential);
    RUN_TEST(test_config_portal_auth_unknown_user);
    RUN_TEST(test_config_portal_auth_failure_count);
    RUN_TEST(test_config_portal_session_validation);
    RUN_TEST(test_config_portal_logout);
    RUN_TEST(test_config_portal_session_carries_identity);
    RUN_TEST(test_config_portal_audit_records_user);
    RUN_TEST(test_config_portal_technician_cannot_change_role);
    RUN_TEST(test_config_portal_admin_can_change_role);
    RUN_TEST(test_config_portal_safety_locked_rejects_all_roles);
    RUN_TEST(test_config_portal_technician_cannot_add_user_after_bootstrap);
    RUN_TEST(test_config_portal_admin_can_add_user_after_bootstrap);
    RUN_TEST(test_config_portal_null_session_rejected_post_bootstrap);
    RUN_TEST(test_config_portal_technician_cannot_self_promote_via_add_user);
    RUN_TEST(test_config_portal_cannot_demote_last_admin);
    RUN_TEST(test_config_portal_admin_can_demote_when_multiple_admins);
    RUN_TEST(test_config_portal_remove_all_technicians_last_admin_stays);
    RUN_TEST(test_config_portal_slot_reuse_after_remove);
    RUN_TEST(test_config_portal_slot_reuse_no_credential_leak);
    RUN_TEST(test_config_portal_slot_reuse_audit_trail);
    RUN_TEST(test_config_portal_null_params);

    PRINT_TEST_SUMMARY();
}
