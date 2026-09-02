/**
 * @file config_portal.h
 * @brief Config portal with per-user RBAC authentication.
 *
 * Architecture ref: Section 4 (T6: field misconfiguration, RBAC baseline/mandatory),
 *                   Section 6 (SAFETY_LOCKED rules immutable from field).
 *
 * Built in from day 1, not a later patch:
 * - Per-user credentials with role-based access control.
 * - Session token carries authenticated user identity and role.
 * - SAFETY_LOCKED rule writes are rejected regardless of role.
 * - Every auth failure and rejected write is audit-logged with user identity.
 *
 * RBAC roles:
 * - ROLE_TECHNICIAN: can edit RULE_CLASS_OPERATIONAL rules only.
 * - ROLE_ADMIN: can additionally manage user accounts (add/remove/change role),
 *   but still cannot edit RULE_CLASS_SAFETY_LOCKED rules.
 *
 * Phase 11: Replaced single shared PIN model (Phases 1-10) with per-user
 * credentials. The legacy config_portal_set_pin() API is removed — see
 * config_portal_add_user() for the replacement.
 *
 * Audit log types and API are in firmware/shared/audit_log.h — this
 * module is a consumer of the audit log, not its owner.
 */

#ifndef CONFIG_PORTAL_H
#define CONFIG_PORTAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../shared/rule_engine_core.h"  /* for RuleClass_t */
#include "../shared/audit_log.h"        /* AuditEventType_t, AuditLogEntry_t, etc. */

/* ---------- Constants ---------- */

#define CONFIG_PORTAL_SESSION_TOKEN_SIZE   32   /**< Session token length. */
#define CONFIG_PORTAL_CREDENTIAL_MAX_LEN   16   /**< Maximum credential length. */
#define CONFIG_PORTAL_HASH_SIZE            32   /**< SHA-256 hash of credential. */
#define CONFIG_PORTAL_MAX_SESSIONS         4    /**< Maximum concurrent sessions. */
#define CONFIG_PORTAL_MAX_USERS            8    /**< Maximum user accounts. */
#define CONFIG_PORTAL_USERNAME_MAX_LEN     16   /**< Maximum username length. */
#define CONFIG_PORTAL_IDLE_TIMEOUT_MS      (15 * 60 * 1000)  /**< 15 minutes default. */

/* ---------- Types ---------- */

/**
 * User roles for RBAC.
 * No role may modify RULE_CLASS_SAFETY_LOCKED rules — this boundary
 * is absolute and not configurable.
 */
typedef enum {
    ROLE_TECHNICIAN = 0,   /**< Can edit RULE_CLASS_OPERATIONAL rules. */
    ROLE_ADMIN      = 1    /**< Can manage user accounts + all technician permissions. */
} UserRole_t;

/**
 * Config portal result codes.
 */
typedef enum {
    CONFIG_OK,
    CONFIG_ERR_AUTH_FAILED,        /**< Credential incorrect. */
    CONFIG_ERR_NO_SESSION,         /**< No active session (not authenticated). */
    CONFIG_ERR_SESSION_EXPIRED,    /**< Session idle timeout exceeded. */
    CONFIG_ERR_SAFETY_LOCKED,      /**< Attempted to modify a SAFETY_LOCKED rule. */
    CONFIG_ERR_NOT_AUTHORIZED,     /**< Role insufficient for this operation. */
    CONFIG_ERR_USER_NOT_FOUND,     /**< Username not found. */
    CONFIG_ERR_USER_EXISTS,        /**< Username already taken. */
    CONFIG_ERR_USERS_FULL,         /**< Maximum user accounts reached. */
    CONFIG_ERR_LAST_ADMIN,         /**< Cannot remove the last admin account. */
    CONFIG_ERR_PARAM_NULL,         /**< NULL pointer argument. */
    CONFIG_ERR_NOT_INIT,           /**< Subsystem not initialised. */
    CONFIG_ERR_INTERNAL            /**< Internal error. */
} ConfigPortalResult_t;

/**
 * User account — per-user credential with role.
 */
typedef struct {
    char     username[CONFIG_PORTAL_USERNAME_MAX_LEN]; /**< Username (null-terminated). */
    uint8_t  credential_hash[CONFIG_PORTAL_HASH_SIZE]; /**< SHA-256 hash of credential. */
    UserRole_t role;                                   /**< User's role. */
    bool     active;                                   /**< Account enabled. */
} UserAccount_t;

/**
 * Storage backend for user accounts.
 * In production, these callbacks would use NVS.
 * For testing, a RAM-backed implementation is provided.
 */
typedef struct {
    /** Save user accounts to persistent storage. */
    bool (*save)(const UserAccount_t *accounts, uint8_t count);
    /** Load user accounts from persistent storage. */
    bool (*load)(UserAccount_t *accounts, uint8_t max_accounts, uint8_t *out_count);
} ConfigPortalStorage_t;

/**
 * Active session context — now carries user identity and role.
 */
typedef struct {
    uint8_t   session_token[CONFIG_PORTAL_SESSION_TOKEN_SIZE];
    uint8_t   session_id;
    uint64_t  last_active_ms;
    bool      active;
    uint8_t   user_index;    /**< Index into user account table. */
    UserRole_t role;         /**< Cached role from authenticated user. */
    char      username[CONFIG_PORTAL_USERNAME_MAX_LEN]; /**< Authenticated username. */
} ConfigSession_t;

/**
 * Configuration write request.
 */
typedef struct {
    uint16_t rule_id;               /**< Target rule to modify. */
    float    new_threshold;         /**< New threshold value. */
    uint8_t  new_action_type;       /**< New action type. */
    uint8_t  new_actuator_id;       /**< New actuator ID. */
} ConfigWriteRequest_t;

/* ---------- API ---------- */

/**
 * Initialise the config portal.
 *
 * @return true on success.
 */
bool config_portal_init(void);

/**
 * Register a storage backend for user account persistence.
 * Must be called before config_portal_init() if persistence is desired.
 *
 * @param storage  Storage backend callbacks.
 */
void config_portal_set_storage(const ConfigPortalStorage_t *storage);

/**
 * Add a user account with credential and role.
 * Credential is hashed with SHA-256 before storage.
 *
 * Bootstrap mode: if the system has zero users (true first-boot state),
 * pass session=NULL to add the first user without authentication.
 * Post-bootstrap: if users already exist, a valid admin session is required.
 *
 * @param session       Active admin session, or NULL for bootstrap (zero users).
 * @param username      Username string.
 * @param username_len  Length of username.
 * @param credential    Credential/password string.
 * @param credential_len Length of credential.
 * @param role          User's role.
 * @param current_ms    Current monotonic timestamp (used for session validation).
 * @return CONFIG_OK on success.
 */
ConfigPortalResult_t config_portal_add_user(
    const ConfigSession_t *session,
    const char *username, size_t username_len,
    const char *credential, size_t credential_len,
    UserRole_t role,
    uint64_t current_ms
);

/**
 * Remove a user account by username.
 * Only ROLE_ADMIN may call this. The last admin account cannot be removed.
 *
 * @param session       Active admin session.
 * @param username      Username to remove.
 * @param username_len  Length of username.
 * @param current_ms    Current monotonic timestamp.
 * @return CONFIG_OK on success.
 */
ConfigPortalResult_t config_portal_remove_user(
    const ConfigSession_t *session,
    const char *username, size_t username_len,
    uint64_t current_ms
);

/**
 * Change a user's role.
 * Only ROLE_ADMIN can perform this operation.
 *
 * @param session       Active admin session.
 * @param username      Username to modify.
 * @param username_len  Length of username.
 * @param new_role      New role to assign.
 * @param current_ms    Current monotonic timestamp.
 * @return CONFIG_OK on success.
 */
ConfigPortalResult_t config_portal_change_role(
    const ConfigSession_t *session,
    const char *username, size_t username_len,
    UserRole_t new_role,
    uint64_t current_ms
);

/**
 * Authenticate with per-user credentials and create a session.
 *
 * @param username          Username string.
 * @param username_len      Length of username.
 * @param credential        Credential/password string.
 * @param credential_len    Length of credential.
 * @param current_ms        Current monotonic timestamp.
 * @param out_session       Output: the new session.
 * @return CONFIG_OK on successful authentication.
 */
ConfigPortalResult_t config_portal_authenticate(
    const char *username, size_t username_len,
    const char *credential, size_t credential_len,
    uint64_t current_ms,
    ConfigSession_t *out_session
);

/**
 * Validate that a session is still active and not expired.
 *
 * @param session     Session to validate.
 * @param current_ms  Current monotonic timestamp.
 * @return CONFIG_OK if session is valid.
 */
ConfigPortalResult_t config_portal_validate_session(
    const ConfigSession_t *session,
    uint64_t current_ms
);

/**
 * Attempt to write a configuration change.
 * REJECTED if the target rule is SAFETY_LOCKED (regardless of role).
 * REJECTED if the session role is ROLE_TECHNICIAN and rule is OPERATIONAL
 * with admin-only semantics (currently all OPERATIONAL rules are writable
 * by technicians — this gate is for future admin-only operational rules).
 *
 * @param session    Active, validated session.
 * @param request    The configuration write request.
 * @param current_ms Current monotonic timestamp.
 * @return CONFIG_OK on success, CONFIG_ERR_SAFETY_LOCKED if rejected.
 */
ConfigPortalResult_t config_portal_write(
    const ConfigSession_t *session,
    const ConfigWriteRequest_t *request,
    uint64_t current_ms
);

/**
 * Reject a write attempt to a SAFETY_LOCKED rule.
 * Logs the rejection to the audit log with user identity.
 *
 * @param session    Active, validated session.
 * @param rule_id    The SAFETY_LOCKED rule that was rejected.
 * @param current_ms Current monotonic timestamp.
 * @return CONFIG_ERR_SAFETY_LOCKED.
 */
ConfigPortalResult_t config_portal_reject_safety_locked(
    const ConfigSession_t *session,
    uint16_t rule_id,
    uint64_t current_ms
);

/**
 * Invalidate (log out) a session.
 *
 * @param session  Session to invalidate.
 */
void config_portal_logout(ConfigSession_t *session);

/**
 * Get the audit log entries.
 * Thin wrapper over audit_log_get_entries() for config-portal callers.
 *
 * @param out_entries   Output array.
 * @param max_entries   Maximum entries to return.
 * @return Number of entries written.
 */
uint8_t config_portal_get_audit_log(AuditLogEntry_t *out_entries, uint8_t max_entries);

/**
 * Get the total number of auth failures since init.
 * Thin wrapper over audit_log_get_auth_failure_count().
 *
 * @return Auth failure count.
 */
uint32_t config_portal_get_auth_failure_count(void);

#endif /* CONFIG_PORTAL_H */
