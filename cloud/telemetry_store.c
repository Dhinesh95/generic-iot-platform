/**
 * @file telemetry_store.c
 * @brief Telemetry store implementation — in-memory tagged store.
 *
 * Architecture ref: Section 7 (Cloud multi-tenancy pipeline).
 *
 * Tenant isolation: every write is tagged with tenant_id, every query
 * filters by tenant_id. Cross-tenant data never leaks.
 *
 * Cardinality tracking: unique series keys (site_id:node_id:metric_id)
 * are counted per tenant. Warnings fire when the count crosses a
 * configurable threshold.
 */

#include "telemetry_store.h"
#include <string.h>
#include <stdio.h>

/* ---------- Internal state ---------- */

static TelemetryDataPoint_t s_store[TELEMETRY_STORE_MAX_SERIES];
static uint32_t s_store_count = 0;
static bool s_initialised = false;

/* Cardinality tracking. */
static TenantCardinality_t s_cardinality[TELEMETRY_STORE_MAX_TENANTS];
static uint8_t s_cardinality_count = 0;

/* Token state. */
static uint32_t s_next_token_id = 1;

/* ---------- Internal helpers ---------- */

/**
 * Generate a unique series key from the tag combination.
 * Two data points with the same site_id:node_id:metric_id are the
 * same series (regardless of timestamp or value).
 */
static void make_series_key(const TelemetryDataPoint_t *point,
                             char *key, size_t key_size)
{
    snprintf(key, key_size, "%s:%s:%s",
             point->site_id, point->node_id, point->metric_id);
}

/**
 * Find or create a cardinality entry for a tenant.
 */
static TenantCardinality_t *find_or_create_cardinality(const char *tenant_id)
{
    /* Look for existing entry. */
    for (uint8_t i = 0; i < s_cardinality_count; i++) {
        if (s_cardinality[i].active &&
            strcmp(s_cardinality[i].tenant_id, tenant_id) == 0) {
            return &s_cardinality[i];
        }
    }

    /* Create new entry. */
    if (s_cardinality_count >= TELEMETRY_STORE_MAX_TENANTS) return NULL;

    TenantCardinality_t *entry = &s_cardinality[s_cardinality_count];
    memset(entry, 0, sizeof(TenantCardinality_t));
    strncpy(entry->tenant_id, tenant_id, TELEMETRY_STORE_MAX_TAG_LEN - 1);
    entry->tenant_id[TELEMETRY_STORE_MAX_TAG_LEN - 1] = '\0';
    entry->active = true;
    s_cardinality_count++;
    return entry;
}

/**
 * Check if a series key already exists for a tenant.
 */
static bool series_key_exists(const char *tenant_id, const char *series_key)
{
    for (uint32_t i = 0; i < s_store_count; i++) {
        if (strcmp(s_store[i].tenant_id, tenant_id) == 0) {
            char existing_key[TELEMETRY_STORE_MAX_TAG_LEN * 3];
            make_series_key(&s_store[i], existing_key, sizeof(existing_key));
            if (strcmp(existing_key, series_key) == 0) {
                return true;
            }
        }
    }
    return false;
}

/* ---------- Public API ---------- */

bool telemetry_store_init(void)
{
    memset(s_store, 0, sizeof(s_store));
    s_store_count = 0;
    memset(s_cardinality, 0, sizeof(s_cardinality));
    s_cardinality_count = 0;
    s_next_token_id = 1;
    s_initialised = true;
    return true;
}

TelemetryStoreResult_t telemetry_store_write(const TelemetryDataPoint_t *point)
{
    if (!s_initialised) return STORE_ERR_PARAM_NULL;
    if (!point) return STORE_ERR_PARAM_NULL;
    if (s_store_count >= TELEMETRY_STORE_MAX_SERIES) return STORE_ERR_FULL;

    /* Store the data point. */
    memcpy(&s_store[s_store_count], point, sizeof(TelemetryDataPoint_t));
    s_store_count++;

    /* Update cardinality tracking. */
    char series_key[TELEMETRY_STORE_MAX_TAG_LEN * 3];
    make_series_key(point, series_key, sizeof(series_key));

    TenantCardinality_t *card = find_or_create_cardinality(point->tenant_id);
    if (card && !series_key_exists(point->tenant_id, series_key)) {
        /* Wait — series_key_exists checks including the new entry we just
         * added. We need to check BEFORE the write, or check if this is
         * the only entry with this key. Let's just always increment if
         * this is the first time we see this key. */
        /* Actually, since we just appended, we need to count how many
         * UNIQUE keys exist for this tenant. Let's just recount. */
        /* Simpler: track whether this key was new before incrementing. */
    }
    /* Recount unique series for this tenant (simple but correct). */
    if (card) {
        uint32_t unique = 0;
        char seen_keys[TELEMETRY_STORE_MAX_SERIES][TELEMETRY_STORE_MAX_TAG_LEN * 3];
        uint32_t seen_count = 0;
        for (uint32_t i = 0; i < s_store_count; i++) {
            if (strcmp(s_store[i].tenant_id, point->tenant_id) != 0) continue;
            char key[TELEMETRY_STORE_MAX_TAG_LEN * 3];
            make_series_key(&s_store[i], key, sizeof(key));
            bool dup = false;
            for (uint32_t j = 0; j < seen_count; j++) {
                if (strcmp(seen_keys[j], key) == 0) { dup = true; break; }
            }
            if (!dup) {
                strncpy(seen_keys[seen_count], key, sizeof(seen_keys[0]) - 1);
                seen_keys[seen_count][sizeof(seen_keys[0]) - 1] = '\0';
                seen_count++;
                unique++;
            }
        }
        card->series_count = unique;
    }

    return STORE_OK;
}

TelemetryStoreResult_t telemetry_issue_tenant_token(
    const char *tenant_id,
    TenantToken_t *out_token)
{
    if (!s_initialised) return STORE_ERR_PARAM_NULL;
    if (!tenant_id || !out_token) return STORE_ERR_PARAM_NULL;

    memset(out_token, 0, sizeof(TenantToken_t));
    out_token->token_id = s_next_token_id++;
    strncpy(out_token->bound_tenant_id, tenant_id, TELEMETRY_STORE_MAX_TAG_LEN - 1);
    out_token->bound_tenant_id[TELEMETRY_STORE_MAX_TAG_LEN - 1] = '\0';
    out_token->valid = true;

    return STORE_OK;
}

TelemetryStoreResult_t telemetry_store_query(
    const TenantToken_t *token,
    TelemetryDataPoint_t *out_points,
    uint8_t max_points,
    uint8_t *out_count)
{
    if (!s_initialised) return STORE_ERR_PARAM_NULL;
    if (!token || !out_points || !out_count) return STORE_ERR_PARAM_NULL;

    /* Authorization check: token must be valid. */
    if (!token->valid) return STORE_ERR_UNAUTHORIZED;
    if (token->bound_tenant_id[0] == '\0') return STORE_ERR_UNAUTHORIZED;

    /* Use the token's bound tenant_id — NOT any caller-supplied string. */
    const char *tenant_id = token->bound_tenant_id;

    *out_count = 0;
    for (uint32_t i = 0; i < s_store_count && *out_count < max_points; i++) {
        if (strcmp(s_store[i].tenant_id, tenant_id) == 0) {
            memcpy(&out_points[*out_count], &s_store[i], sizeof(TelemetryDataPoint_t));
            (*out_count)++;
        }
    }

    return STORE_OK;
}

uint32_t telemetry_store_get_cardinality(const char *tenant_id)
{
    if (!s_initialised || !tenant_id) return 0;

    for (uint8_t i = 0; i < s_cardinality_count; i++) {
        if (s_cardinality[i].active &&
            strcmp(s_cardinality[i].tenant_id, tenant_id) == 0) {
            return s_cardinality[i].series_count;
        }
    }
    return 0;
}

uint8_t telemetry_store_get_all_cardinality(
    TenantCardinality_t *out_entries,
    uint8_t max_entries)
{
    if (!s_initialised || !out_entries) return 0;

    uint8_t count = (s_cardinality_count < max_entries)
                  ? s_cardinality_count : max_entries;
    memcpy(out_entries, s_cardinality, sizeof(TenantCardinality_t) * count);
    return count;
}

bool telemetry_store_cardinality_exceeds(
    const char *tenant_id,
    uint32_t threshold)
{
    return telemetry_store_get_cardinality(tenant_id) > threshold;
}
