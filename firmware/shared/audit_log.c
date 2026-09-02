/**
 * @file audit_log.c
 * @brief Audit log implementation — tamper-evident event logging.
 *
 * Architecture ref: Section 4 (mandatory for all domains).
 *
 * Moved from firmware/hub/config_portal.c in Phase 6.7 to correct
 * the dependency direction — shared/core code must not depend on
 * Hub-tier files.
 */

#include "audit_log.h"
#include "attestation.h"  /* for iot_hmac_sha256 */
#include "time_source.h"
#include <string.h>

/* ---------- Internal state ---------- */

static AuditLogEntry_t g_audit_log[AUDIT_LOG_MAX_ENTRIES];
static uint8_t g_audit_write_index = 0;
static uint32_t g_auth_failure_count = 0;
static uint8_t g_chain_key[32];  /**< HMAC key for chain verification. */
static bool g_chain_key_set = false;
static bool g_initialised = false;

static const AuditLogStorage_t *g_storage = NULL;

/* ---------- Internal helpers ---------- */

static void audit_log_persist(void)
{
    if (g_storage && g_storage->save) {
        uint8_t count = (g_audit_write_index < AUDIT_LOG_MAX_ENTRIES)
                      ? g_audit_write_index
                      : AUDIT_LOG_MAX_ENTRIES;
        g_storage->save(g_audit_log, count, g_audit_write_index, g_auth_failure_count);
    }
}

/**
 * Compute chain HMAC for an audit log entry.
 * chain_hmac = HMAC(key, entry_content || previous_chain_hmac)
 */
static void audit_log_compute_chain_hmac(AuditLogEntry_t *entry, uint8_t prev_hmac[AUDIT_LOG_HMAC_SIZE])
{
    if (!g_chain_key_set) {
        memset(entry->chain_hmac, 0, AUDIT_LOG_HMAC_SIZE);
        return;
    }

    /* Build payload: entry content (without chain_hmac) + previous HMAC */
    size_t content_size = offsetof(AuditLogEntry_t, chain_hmac);
    size_t total_size = content_size + AUDIT_LOG_HMAC_SIZE;

    uint8_t buf[sizeof(AuditLogEntry_t) + AUDIT_LOG_HMAC_SIZE];
    memcpy(buf, entry, content_size);
    memcpy(buf + content_size, prev_hmac, AUDIT_LOG_HMAC_SIZE);

    iot_hmac_sha256(g_chain_key, sizeof(g_chain_key), buf, total_size, entry->chain_hmac);
}

/* ---------- Public API ---------- */

bool audit_log_init(void)
{
    g_audit_write_index = 0;
    g_auth_failure_count = 0;
    g_chain_key_set = false;

    /* Load persisted audit log if storage backend is registered. */
    if (g_storage && g_storage->load) {
        g_storage->load(g_audit_log, AUDIT_LOG_MAX_ENTRIES,
                        &g_audit_write_index, &g_auth_failure_count);
    } else {
        memset(g_audit_log, 0, sizeof(g_audit_log));
    }

    g_initialised = true;
    return true;
}

void audit_log_set_storage(const AuditLogStorage_t *storage)
{
    g_storage = storage;
}

void audit_log_add(AuditEventType_t event_type, uint8_t session_id,
                   uint16_t rule_id, const char *detail)
{
    if (!g_initialised) return;

    AuditLogEntry_t *entry = &g_audit_log[g_audit_write_index % AUDIT_LOG_MAX_ENTRIES];

    /* Get previous entry's chain HMAC for chaining. */
    uint8_t prev_hmac[AUDIT_LOG_HMAC_SIZE];
    if (g_audit_write_index > 0) {
        uint8_t prev_idx = (g_audit_write_index - 1) % AUDIT_LOG_MAX_ENTRIES;
        memcpy(prev_hmac, g_audit_log[prev_idx].chain_hmac, AUDIT_LOG_HMAC_SIZE);
    } else {
        memset(prev_hmac, 0, AUDIT_LOG_HMAC_SIZE);
    }

    entry->timestamp_ms = time_source_get_ms();
    entry->event_type = event_type;
    entry->session_id = session_id;
    entry->rule_id = rule_id;
    if (detail) {
        strncpy(entry->detail, detail, sizeof(entry->detail) - 1);
        entry->detail[sizeof(entry->detail) - 1] = '\0';
    } else {
        entry->detail[0] = '\0';
    }

    /* Track auth failures for monitoring. */
    if (event_type == AUDIT_AUTH_FAILURE) {
        g_auth_failure_count++;
    }

    /* Compute chain HMAC over entry content + previous HMAC. */
    audit_log_compute_chain_hmac(entry, prev_hmac);

    g_audit_write_index++;

    /* Persist to storage after every write. */
    audit_log_persist();
}

uint8_t audit_log_get_count(void)
{
    if (!g_initialised) return 0;

    uint8_t count = (g_audit_write_index < AUDIT_LOG_MAX_ENTRIES)
                  ? g_audit_write_index
                  : AUDIT_LOG_MAX_ENTRIES;
    return count;
}

uint8_t audit_log_get_entries(AuditLogEntry_t *out_entries, uint8_t max_entries)
{
    if (!out_entries || max_entries == 0) return 0;
    if (!g_initialised) return 0;

    uint8_t count = (g_audit_write_index < AUDIT_LOG_MAX_ENTRIES)
                  ? g_audit_write_index
                  : AUDIT_LOG_MAX_ENTRIES;

    uint8_t to_copy = (count < max_entries) ? count : max_entries;
    uint8_t start_idx = (g_audit_write_index >= AUDIT_LOG_MAX_ENTRIES)
                      ? (g_audit_write_index % AUDIT_LOG_MAX_ENTRIES)
                      : 0;

    for (uint8_t i = 0; i < to_copy; i++) {
        uint8_t src_idx = (start_idx + i) % AUDIT_LOG_MAX_ENTRIES;
        memcpy(&out_entries[i], &g_audit_log[src_idx], sizeof(AuditLogEntry_t));
    }

    return to_copy;
}

uint32_t audit_log_get_auth_failure_count(void)
{
    return g_auth_failure_count;
}

/* ---------- Audit log HMAC chain verification ---------- */

bool audit_log_verify_chain(const uint8_t *hmac_key, size_t key_len)
{
    if (!hmac_key || key_len == 0) return false;
    if (!g_initialised) return false;

    uint8_t count = audit_log_get_count();
    if (count == 0) return true;  /* Empty log is valid. */

    uint8_t prev_hmac[AUDIT_LOG_HMAC_SIZE];
    memset(prev_hmac, 0, AUDIT_LOG_HMAC_SIZE);

    uint8_t start_idx = (g_audit_write_index >= AUDIT_LOG_MAX_ENTRIES)
                      ? (g_audit_write_index % AUDIT_LOG_MAX_ENTRIES)
                      : 0;

    for (uint8_t i = 0; i < count; i++) {
        uint8_t src_idx = (start_idx + i) % AUDIT_LOG_MAX_ENTRIES;
        const AuditLogEntry_t *entry = &g_audit_log[src_idx];

        /* Recompute expected chain HMAC. */
        size_t content_size = offsetof(AuditLogEntry_t, chain_hmac);
        size_t total_size = content_size + AUDIT_LOG_HMAC_SIZE;

        uint8_t buf[sizeof(AuditLogEntry_t) + AUDIT_LOG_HMAC_SIZE];
        memcpy(buf, entry, content_size);
        memcpy(buf + content_size, prev_hmac, AUDIT_LOG_HMAC_SIZE);

        uint8_t expected_hmac[AUDIT_LOG_HMAC_SIZE];
        iot_hmac_sha256(hmac_key, key_len, buf, total_size, expected_hmac);

        /* Compare with stored HMAC. */
        uint8_t diff = 0;
        for (int j = 0; j < AUDIT_LOG_HMAC_SIZE; j++) {
            diff |= entry->chain_hmac[j] ^ expected_hmac[j];
        }

        if (diff != 0) {
            return false;  /* Chain broken — tampering detected. */
        }

        /* Update prev_hmac for next iteration. */
        memcpy(prev_hmac, entry->chain_hmac, AUDIT_LOG_HMAC_SIZE);
    }

    return true;  /* Chain intact. */
}

/* ---------- Chain key management ---------- */

void audit_log_set_chain_key(const uint8_t *key, size_t key_len)
{
    if (!key || key_len == 0) return;
    size_t copy_len = (key_len < sizeof(g_chain_key)) ? key_len : sizeof(g_chain_key);
    memcpy(g_chain_key, key, copy_len);
    /* Zero-pad remaining bytes. */
    if (copy_len < sizeof(g_chain_key)) {
        memset(g_chain_key + copy_len, 0, sizeof(g_chain_key) - copy_len);
    }
    g_chain_key_set = true;
}
