/**
 * @file telemetry_store.h
 * @brief Telemetry store — mock time-series store with tenant isolation.
 *
 * Architecture ref: Section 7 (Cloud multi-tenancy pipeline).
 *
 * In production, this would be InfluxDB. For testing and sandbox
 * environments, this is an in-memory tagged key-value store that
 * enforces the same tenant-scoping logic. The real InfluxDB integration
 * is a tracked pre-deployment item.
 *
 * Schema per Section 7:
 *   measurement: node_telemetry
 *   tags: tenant_id, site_id, node_id, device_class, metric_id, domain_profile_id
 *   fields: value (float), timestamp_ms (uint64_t)
 */

#ifndef TELEMETRY_STORE_H
#define TELEMETRY_STORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---------- Constants ---------- */

#define TELEMETRY_STORE_MAX_SERIES     1024  /**< Max stored series entries. */
#define TELEMETRY_STORE_MAX_TAG_LEN    64   /**< Max tag string length. */
#define TELEMETRY_STORE_MAX_TENANTS    16   /**< Max tracked tenants. */

/* ---------- Types ---------- */

/**
 * A single telemetry data point (one series entry).
 */
typedef struct {
    char     tenant_id[TELEMETRY_STORE_MAX_TAG_LEN];
    char     site_id[TELEMETRY_STORE_MAX_TAG_LEN];
    char     node_id[TELEMETRY_STORE_MAX_TAG_LEN];
    char     device_class[TELEMETRY_STORE_MAX_TAG_LEN];
    char     metric_id[TELEMETRY_STORE_MAX_TAG_LEN];
    char     domain_profile_id[TELEMETRY_STORE_MAX_TAG_LEN];
    float    value;
    uint64_t timestamp_ms;
} TelemetryDataPoint_t;

/**
 * Cardinality tracking per tenant.
 */
typedef struct {
    char     tenant_id[TELEMETRY_STORE_MAX_TAG_LEN];
    uint32_t series_count;       /**< Unique series keys seen for this tenant. */
    bool     active;
} TenantCardinality_t;

/**
 * Tenant authorization token.
 * Scoped to exactly one tenant_id at issuance. The caller cannot
 * specify an arbitrary tenant_id independent of their token —
 * the token's bound tenant_id is used internally.
 */
typedef struct {
    uint32_t token_id;                        /**< Unique token identifier. */
    char     bound_tenant_id[TELEMETRY_STORE_MAX_TAG_LEN]; /**< Tenant this token is authorized for. */
    bool     valid;                           /**< false = revoked. */
} TenantToken_t;

/**
 * Result of store operations.
 */
typedef enum {
    STORE_OK,
    STORE_ERR_FULL,              /**< Store is at capacity. */
    STORE_ERR_PARAM_NULL,        /**< NULL pointer argument. */
    STORE_ERR_TENANT_NOT_FOUND,  /**< Tenant not found for query. */
    STORE_ERR_UNAUTHORIZED       /**< Token not authorized for this tenant. */
} TelemetryStoreResult_t;

/* ---------- API ---------- */

/**
 * Initialise the telemetry store.
 *
 * @return true on success.
 */
bool telemetry_store_init(void);

/**
 * Write a telemetry data point to the store.
 * The data point is tagged with tenant_id — queries are scoped to tenant.
 *
 * @param point  The data point to store.
 * @return STORE_OK on success.
 */
TelemetryStoreResult_t telemetry_store_write(const TelemetryDataPoint_t *point);

/**
 * Issue a tenant authorization token.
 * The token is bound to exactly one tenant_id — queries using this
 * token will only access that tenant's data.
 *
 * @param tenant_id   Tenant to authorize the token for.
 * @param out_token   Output: the issued token.
 * @return STORE_OK on success.
 */
TelemetryStoreResult_t telemetry_issue_tenant_token(
    const char *tenant_id,
    TenantToken_t *out_token
);

/**
 * Query telemetry data points, authorized by a tenant token.
 * The query is scoped to the token's bound tenant_id — the caller
 * cannot specify an arbitrary tenant_id. Cross-tenant data is never
 * returned, and unauthorized access attempts return STORE_ERR_UNAUTHORIZED.
 *
 * @param token       Authorization token (must be valid and non-NULL).
 * @param out_points  Output array.
 * @param max_points  Maximum points to return.
 * @param out_count   Output: actual number of points returned.
 * @return STORE_OK on success, STORE_ERR_UNAUTHORIZED if token is invalid
 *         or not bound to any tenant with data.
 */
TelemetryStoreResult_t telemetry_store_query(
    const TenantToken_t *token,
    TelemetryDataPoint_t *out_points,
    uint8_t max_points,
    uint8_t *out_count
);

/**
 * Get the cardinality (unique series count) for a tenant.
 *
 * @param tenant_id  Tenant to check.
 * @return Number of unique series for this tenant.
 */
uint32_t telemetry_store_get_cardinality(const char *tenant_id);

/**
 * Get all tracked tenant cardinality entries.
 *
 * @param out_entries   Output array.
 * @param max_entries   Maximum entries to return.
 * @return Number of entries written.
 */
uint8_t telemetry_store_get_all_cardinality(
    TenantCardinality_t *out_entries,
    uint8_t max_entries
);

/**
 * Check if a tenant's series count exceeds a threshold.
 *
 * @param tenant_id   Tenant to check.
 * @param threshold   Series count threshold.
 * @return true if cardinality exceeds threshold.
 */
bool telemetry_store_cardinality_exceeds(
    const char *tenant_id,
    uint32_t threshold
);

#endif /* TELEMETRY_STORE_H */
