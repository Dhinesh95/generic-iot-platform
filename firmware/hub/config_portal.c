/**
 * @file config_portal.c
 * @brief Config portal implementation — per-user RBAC authentication,
 *        role-based permission checks, safety-locked rule enforcement.
 *
 * Architecture ref: Section 4 (T6, RBAC baseline/mandatory),
 *                   Section 6 (SAFETY_LOCKED).
 *
 * Phase 11: Replaced single shared PIN model with per-user credentials.
 * Phase 11.1: Added role-gate to remove_user (admin-only) + last-admin safeguard.
 * Phase 11.2: Added bootstrap/admin-gate to add_user — zero users = no session
 *   required (first-boot), else ROLE_ADMIN required (prevents technician
 *   privilege escalation via self-creating an admin account).
 * Phase 11.3: Added last-admin safeguard to change_role — cannot demote the
 *   sole remaining ROLE_ADMIN (would create a zero-admin, nonzero-user state
 *   with no recovery path). Corrected inaccurate Phase 11.2 claim about
 *   bootstrap re-entry being reachable via remove_user — g_user_count is never
 *   decremented, so bootstrap mode (g_user_count == 0) is only reachable on
 *   first boot, never via user removal.
 *
 * Every auth failure and every rejected write is audit-logged via the
 * shared audit log module (firmware/shared/audit_log.h) with the
 * acting user's identity.
 */

#include "config_portal.h"
#include "../shared/attestation.h"  /* for iot_hmac_sha256 */
#include "../shared/audit_log.h"    /* audit log subsystem */
#include <string.h>

/* ---------- Internal state ---------- */

static UserAccount_t g_users[CONFIG_PORTAL_MAX_USERS];
static uint8_t g_user_count = 0;
static bool g_initialised = false;

static ConfigSession_t g_sessions[CONFIG_PORTAL_MAX_SESSIONS];
static uint8_t g_next_session_id = 1;

static const ConfigPortalStorage_t *g_storage = NULL;

/* ---------- Internal helpers ---------- */

static void hash_credential(const char *credential, size_t cred_len,
                            uint8_t out_hash[CONFIG_PORTAL_HASH_SIZE])
{
    static const uint8_t salt[] = "config_portal_credential_salt_v2";
    iot_hmac_sha256(salt, sizeof(salt) - 1,
                    (const uint8_t *)credential, cred_len, out_hash);
}

static int find_user(const char *username, size_t username_len)
{
    for (uint8_t i = 0; i < g_user_count; i++) {
        if (g_users[i].active &&
            strlen(g_users[i].username) == username_len &&
            memcmp(g_users[i].username, username, username_len) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void persist_users(void)
{
    if (g_storage && g_storage->save) {
        g_storage->save(g_users, g_user_count);
    }
}

static int count_active_admins(void)
{
    int count = 0;
    for (uint8_t i = 0; i < g_user_count; i++) {
        if (g_users[i].active && g_users[i].role == ROLE_ADMIN)
            count++;
    }
    return count;
}

/* ---------- Public API ---------- */

bool config_portal_init(void)
{
    memset(g_users, 0, sizeof(g_users));
    memset(g_sessions, 0, sizeof(g_sessions));
    g_user_count = 0;
    g_next_session_id = 1;

    /* Load persisted user accounts if storage backend is registered. */
    if (g_storage && g_storage->load) {
        g_storage->load(g_users, CONFIG_PORTAL_MAX_USERS, &g_user_count);
    }

    /* Initialise the shared audit log subsystem. */
    audit_log_init();

    g_initialised = true;
    return true;
}

void config_portal_set_storage(const ConfigPortalStorage_t *storage)
{
    g_storage = storage;
}

ConfigPortalResult_t config_portal_add_user(
    const ConfigSession_t *session,
    const char *username, size_t username_len,
    const char *credential, size_t credential_len,
    UserRole_t role,
    uint64_t current_ms)
{
    if (!username || !credential) return CONFIG_ERR_PARAM_NULL;
    if (!g_initialised) return CONFIG_ERR_NOT_INIT;
    if (username_len == 0 || username_len >= CONFIG_PORTAL_USERNAME_MAX_LEN)
        return CONFIG_ERR_PARAM_NULL;
    if (credential_len == 0 || credential_len > CONFIG_PORTAL_CREDENTIAL_MAX_LEN)
        return CONFIG_ERR_PARAM_NULL;

    /*
     * Bootstrap vs post-bootstrap gate:
     * - Zero users (true first-boot): allow without session (NULL is fine).
     *   This prevents the system from being bricked on first boot.
     * - Users exist: require a valid admin session. A NULL session here
     *   is NOT a bootstrap call — it means "no authentication provided"
     *   and must be rejected. This closes the technician-privilege-
     *   escalation attack where a technician could call add_user to
     *   create a ROLE_ADMIN account for themselves.
     */
    if (g_user_count > 0) {
        /* Post-bootstrap: require admin session. */
        if (!session) {
            audit_log_add(AUDIT_AUTH_FAILURE, 0, 0,
                          "Add user denied: no session (post-bootstrap)");
            return CONFIG_ERR_NOT_AUTHORIZED;
        }
        if (session->role != ROLE_ADMIN) {
            audit_log_add(AUDIT_SAFETY_LOCKED_REJECTED, session->session_id,
                          0, "Add user denied: not admin");
            return CONFIG_ERR_NOT_AUTHORIZED;
        }
        /* Validate session is still active. */
        ConfigPortalResult_t session_result = config_portal_validate_session(session, current_ms);
        if (session_result != CONFIG_OK) return session_result;
    }

    /* Check for duplicate username. */
    if (find_user(username, username_len) >= 0)
        return CONFIG_ERR_USER_EXISTS;

    /*
     * Slot allocation: scan for an inactive (freed) slot first.
     * If found, reuse it. Otherwise, allocate a new slot at g_user_count.
     * g_user_count is a high-water mark — it only increases, never decreases.
     * This allows slot reuse after remove_user() without changing the
     * high-water-mark semantics that find_user() and the bootstrap check
     * depend on.
     */
    int slot = -1;
    for (uint8_t i = 0; i < g_user_count; i++) {
        if (!g_users[i].active) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0) {
        /* No inactive slot found — allocate a new one. */
        if (g_user_count >= CONFIG_PORTAL_MAX_USERS)
            return CONFIG_ERR_USERS_FULL;
        slot = (int)g_user_count;
        g_user_count++;
    }

    UserAccount_t *user = &g_users[slot];
    memset(user, 0, sizeof(*user));
    memcpy(user->username, username, username_len);
    user->username[username_len] = '\0';
    hash_credential(credential, credential_len, user->credential_hash);
    user->role = role;
    user->active = true;

    persist_users();

    audit_log_add(AUDIT_CONFIG_WRITE, 0, 0, "User account added");
    return CONFIG_OK;
}

static int count_active_admins_for_remove(void)
{
    return count_active_admins();
}

ConfigPortalResult_t config_portal_remove_user(
    const ConfigSession_t *session,
    const char *username, size_t username_len,
    uint64_t current_ms)
{
    if (!session || !username) return CONFIG_ERR_PARAM_NULL;
    if (!g_initialised) return CONFIG_ERR_NOT_INIT;

    /* Only admins can remove users. */
    if (session->role != ROLE_ADMIN) {
        audit_log_add(AUDIT_SAFETY_LOCKED_REJECTED, session->session_id,
                      0, "Remove user denied: not admin");
        return CONFIG_ERR_NOT_AUTHORIZED;
    }

    /* Validate session. */
    ConfigPortalResult_t session_result = config_portal_validate_session(session, current_ms);
    if (session_result != CONFIG_OK) return session_result;

    int idx = find_user(username, username_len);
    if (idx < 0) return CONFIG_ERR_USER_NOT_FOUND;

    /* Last-admin safeguard: cannot remove the last admin account. */
    if (g_users[idx].role == ROLE_ADMIN && count_active_admins_for_remove() <= 1) {
        audit_log_add(AUDIT_SAFETY_LOCKED_REJECTED, session->session_id,
                      0, "Remove user denied: last admin");
        return CONFIG_ERR_LAST_ADMIN;
    }

    g_users[idx].active = false;
    memset(g_users[idx].credential_hash, 0, CONFIG_PORTAL_HASH_SIZE);
    persist_users();

    /*
     * NOTE: g_user_count is NOT decremented here. It is a high-water mark
     * (monotonically increasing) that tracks the total number of user slots
     * ever allocated, not the number of currently-active users. This means:
     * - g_user_count never reaches 0 after first boot (only init() resets it).
     * - The bootstrap check in add_user() (g_user_count > 0) is permanently
     *   true after the first user is added.
     * - find_user() iterates over g_user_count slots, skipping inactive ones.
     * - Slot reuse: add_user() scans for inactive (freed) slots before
     *   allocating a new one, so removals do permanently reclaim slot
     *   capacity. CONFIG_PORTAL_MAX_USERS is the ceiling on total slots
     *   ever allocated (high-water mark), not on concurrent active users.
     *
     * The last-admin safeguard above (CONFIG_ERR_LAST_ADMIN) is the only
     * thing preventing the system from having zero active admins. Without
     * it, g_user_count would still be > 0 (bootstrap unreachable), but
     * count_active_admins() would be 0 — a worse state than bootstrap
     * because recovery is impossible. Phase 11.3's change_role safeguard
     * closes the same gap for demotion.
     */

    audit_log_add(AUDIT_CONFIG_WRITE, session->session_id, 0, "User account removed");
    return CONFIG_OK;
}

ConfigPortalResult_t config_portal_change_role(
    const ConfigSession_t *session,
    const char *username, size_t username_len,
    UserRole_t new_role,
    uint64_t current_ms)
{
    if (!session || !username) return CONFIG_ERR_PARAM_NULL;
    if (!g_initialised) return CONFIG_ERR_NOT_INIT;

    /* Only admins can change roles. */
    if (session->role != ROLE_ADMIN) {
        audit_log_add(AUDIT_SAFETY_LOCKED_REJECTED, session->session_id,
                      0, "Role change denied: not admin");
        return CONFIG_ERR_NOT_AUTHORIZED;
    }

    /* Validate session. */
    ConfigPortalResult_t session_result = config_portal_validate_session(session, current_ms);
    if (session_result != CONFIG_OK) return session_result;

    int idx = find_user(username, username_len);
    if (idx < 0) return CONFIG_ERR_USER_NOT_FOUND;

    /*
     * Last-admin safeguard: cannot demote the sole remaining admin.
     * If this check were absent, an admin could call:
     *   change_role(sole_admin, ROLE_TECHNICIAN)
     * leaving count_active_admins() == 0 while g_user_count > 0.
     * Since add_user() requires g_user_count > 0 to be admin-gated
     * (post-bootstrap), and no admin session can exist, the system
     * would be permanently locked out of user management — worse than
     * bootstrap, which at least allows recovery via NULL session.
     *
     * This mirrors the same-last-admin protection on remove_user().
     */
    if (g_users[idx].role == ROLE_ADMIN && new_role != ROLE_ADMIN &&
        count_active_admins_for_remove() <= 1) {
        audit_log_add(AUDIT_SAFETY_LOCKED_REJECTED, session->session_id,
                      0, "Role change denied: last admin cannot be demoted");
        return CONFIG_ERR_LAST_ADMIN;
    }

    g_users[idx].role = new_role;
    persist_users();

    audit_log_add(AUDIT_CONFIG_WRITE, session->session_id, 0, "User role changed");
    return CONFIG_OK;
}

ConfigPortalResult_t config_portal_authenticate(
    const char *username, size_t username_len,
    const char *credential, size_t credential_len,
    uint64_t current_ms,
    ConfigSession_t *out_session)
{
    if (!username || !credential || !out_session) return CONFIG_ERR_PARAM_NULL;
    if (!g_initialised) return CONFIG_ERR_NOT_INIT;

    /* Find the user. */
    int idx = find_user(username, username_len);
    if (idx < 0) {
        audit_log_add(AUDIT_AUTH_FAILURE, 0, 0, "Username not found");
        return CONFIG_ERR_AUTH_FAILED;
    }

    /* Verify credential. */
    uint8_t provided_hash[CONFIG_PORTAL_HASH_SIZE];
    hash_credential(credential, credential_len, provided_hash);

    uint8_t diff = 0;
    for (int i = 0; i < CONFIG_PORTAL_HASH_SIZE; i++) {
        diff |= (uint8_t)(provided_hash[i] ^ g_users[idx].credential_hash[i]);
    }

    if (diff != 0) {
        audit_log_add(AUDIT_AUTH_FAILURE, 0, 0, "Credential authentication failed");
        return CONFIG_ERR_AUTH_FAILED;
    }

    /* Find a free session slot. */
    ConfigSession_t *session = NULL;
    for (int i = 0; i < CONFIG_PORTAL_MAX_SESSIONS; i++) {
        if (!g_sessions[i].active) {
            session = &g_sessions[i];
            break;
        }
    }

    if (!session) {
        audit_log_add(AUDIT_AUTH_FAILURE, 0, 0, "No free session slots");
        return CONFIG_ERR_INTERNAL;
    }

    /* Create session with user identity and role. */
    session->session_id = g_next_session_id++;
    session->last_active_ms = current_ms;
    session->active = true;
    session->user_index = (uint8_t)idx;
    session->role = g_users[idx].role;
    strncpy(session->username, g_users[idx].username, CONFIG_PORTAL_USERNAME_MAX_LEN - 1);
    session->username[CONFIG_PORTAL_USERNAME_MAX_LEN - 1] = '\0';

    /* Generate session token (simplified — use random in production). */
    for (int i = 0; i < CONFIG_PORTAL_SESSION_TOKEN_SIZE; i++) {
        session->session_token[i] = (uint8_t)(session->session_id * 31 + i * 17 + current_ms);
    }

    audit_log_add(AUDIT_SESSION_CREATED, session->session_id, 0, "Session created");
    audit_log_add(AUDIT_AUTH_SUCCESS, session->session_id, 0, username);

    memcpy(out_session, session, sizeof(ConfigSession_t));
    return CONFIG_OK;
}

ConfigPortalResult_t config_portal_validate_session(
    const ConfigSession_t *session,
    uint64_t current_ms)
{
    if (!session) return CONFIG_ERR_PARAM_NULL;
    if (!g_initialised) return CONFIG_ERR_NOT_INIT;
    if (!session->active) return CONFIG_ERR_NO_SESSION;

    if ((current_ms - session->last_active_ms) > CONFIG_PORTAL_IDLE_TIMEOUT_MS) {
        return CONFIG_ERR_SESSION_EXPIRED;
    }

    return CONFIG_OK;
}

ConfigPortalResult_t config_portal_write(
    const ConfigSession_t *session,
    const ConfigWriteRequest_t *request,
    uint64_t current_ms)
{
    if (!session || !request) return CONFIG_ERR_PARAM_NULL;
    if (!g_initialised) return CONFIG_ERR_NOT_INIT;

    ConfigPortalResult_t session_result = config_portal_validate_session(session, current_ms);
    if (session_result != CONFIG_OK) return session_result;

    (void)current_ms;
    (void)request;

    audit_log_add(AUDIT_CONFIG_WRITE, session->session_id,
                  request->rule_id, "Config write accepted");

    return CONFIG_OK;
}

ConfigPortalResult_t config_portal_reject_safety_locked(
    const ConfigSession_t *session,
    uint16_t rule_id,
    uint64_t current_ms)
{
    if (!session) return CONFIG_ERR_PARAM_NULL;
    if (!g_initialised) return CONFIG_ERR_NOT_INIT;

    ConfigPortalResult_t session_result = config_portal_validate_session(session, current_ms);
    if (session_result != CONFIG_OK) return session_result;

    audit_log_add(AUDIT_SAFETY_LOCKED_REJECTED, session->session_id,
                  rule_id, "Write rejected: SAFETY_LOCKED rule is immutable from field");

    return CONFIG_ERR_SAFETY_LOCKED;
}

void config_portal_logout(ConfigSession_t *session)
{
    if (session && session->active) {
        audit_log_add(AUDIT_SESSION_EXPIRED, session->session_id, 0, "Session logged out");
        session->active = false;
        memset(session->session_token, 0, CONFIG_PORTAL_SESSION_TOKEN_SIZE);
    }
}

/* ---------- Thin wrappers over audit_log module ---------- */

uint8_t config_portal_get_audit_log(AuditLogEntry_t *out_entries, uint8_t max_entries)
{
    return audit_log_get_entries(out_entries, max_entries);
}

uint32_t config_portal_get_auth_failure_count(void)
{
    return audit_log_get_auth_failure_count();
}
