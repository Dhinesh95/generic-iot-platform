/**
 * @file audit_log.h
 * @brief Audit log — tamper-evident event logging with HMAC chain.
 *
 * Architecture ref: Section 4 (mandatory for all domains).
 *
 * This is a CORE module, not Hub-specific. Audit logging is a
 * cross-cutting concern used by config-portal auth, tamper detection,
 * LoRa join failures, and any future subsystem that needs a tamper-
 * evident event record.
 *
 * Extracted from firmware/hub/config_portal.h in Phase 6.7 to correct
 * the dependency direction — shared/core code must not depend on
 * Hub-tier files.
 */

#ifndef AUDIT_LOG_H
#define AUDIT_LOG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---------- Constants ---------- */

#define AUDIT_LOG_MAX_ENTRIES    32   /**< Audit log depth. */
#define AUDIT_LOG_HMAC_SIZE      32   /**< HMAC-SHA256 size for chain. */

/* ---------- Types ---------- */

/**
 * Audit log event types.
 */
typedef enum {
    AUDIT_AUTH_SUCCESS = 0,
    AUDIT_AUTH_FAILURE,
    AUDIT_SAFETY_LOCKED_REJECTED,
    AUDIT_CONFIG_WRITE,
    AUDIT_SESSION_CREATED,
    AUDIT_SESSION_EXPIRED,
    AUDIT_TAMPER_DETECTED,          /**< Physical tamper switch triggered. */
    AUDIT_MODULE_ATTESTATION_FAIL  /**< Radio module attestation failed. */
} AuditEventType_t;

/**
 * Audit log entry.
 */
typedef struct {
    uint64_t timestamp_ms;
    AuditEventType_t event_type;
    uint8_t  session_id;            /**< Session that generated this event (0 = none). */
    uint16_t rule_id;               /**< Rule involved (for safety-locked rejections). 0 = N/A. */
    char     detail[64];            /**< Human-readable detail string. */
    uint8_t  chain_hmac[AUDIT_LOG_HMAC_SIZE]; /**< HMAC chain (tamper-evident). */
} AuditLogEntry_t;

/**
 * Storage backend for audit log persistence.
 * In production, these callbacks would use NVS or LittleFS.
 * For testing, a RAM-backed implementation is provided.
 */
typedef struct {
    /** Save audit log entries to persistent storage. */
    bool (*save)(const AuditLogEntry_t *entries, uint8_t count,
                 uint8_t write_index, uint32_t auth_failure_count);
    /** Load audit log entries from persistent storage. */
    bool (*load)(AuditLogEntry_t *entries, uint8_t max_entries,
                 uint8_t *write_index, uint32_t *auth_failure_count);
} AuditLogStorage_t;

/* ---------- API ---------- */

/**
 * Initialise the audit log subsystem.
 * Clears all entries, resets counters, loads from storage if registered.
 *
 * @return true on success.
 */
bool audit_log_init(void);

/**
 * Add an entry to the audit log.
 *
 * @param event_type  Type of event.
 * @param session_id  Session that generated this event (0 = none).
 * @param rule_id     Rule involved (0 = N/A).
 * @param detail      Human-readable detail string (may be NULL).
 */
void audit_log_add(AuditEventType_t event_type, uint8_t session_id,
                   uint16_t rule_id, const char *detail);

/**
 * Audit log HMAC chain verification.
 *
 * Verifies that the HMAC chain is intact — each entry's chain_hmac
 * is computed over its content plus the previous entry's chain_hmac.
 * Tampering with any historical entry is detectable.
 *
 * @param hmac_key  Key used for HMAC chain computation.
 * @param key_len   Length of HMAC key.
 * @return true if chain is valid, false if tampering detected.
 */
bool audit_log_verify_chain(const uint8_t *hmac_key, size_t key_len);

/**
 * Get the number of audit log entries currently stored.
 *
 * @return Number of entries.
 */
uint8_t audit_log_get_count(void);

/**
 * Set the HMAC key used for chain verification.
 * Must be called before any audit log entries are created.
 *
 * @param key      Key bytes.
 * @param key_len  Length of key.
 */
void audit_log_set_chain_key(const uint8_t *key, size_t key_len);

/**
 * Register a storage backend for audit log persistence.
 * Must be called before audit_log_init() if persistence is desired.
 *
 * @param storage  Storage backend callbacks. Pass NULL to disable persistence.
 */
void audit_log_set_storage(const AuditLogStorage_t *storage);

/**
 * Get the audit log entries.
 *
 * @param out_entries   Output array.
 * @param max_entries   Maximum entries to return.
 * @return Number of entries written.
 */
uint8_t audit_log_get_entries(AuditLogEntry_t *out_entries, uint8_t max_entries);

/**
 * Get the total number of auth failures since init.
 *
 * @return Auth failure count.
 */
uint32_t audit_log_get_auth_failure_count(void);

#endif /* AUDIT_LOG_H */
