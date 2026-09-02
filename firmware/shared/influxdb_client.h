/**
 * @file influxdb_client.h
 * @brief InfluxDB 2.x line-protocol client with mockable HTTP transport.
 *
 * Architecture ref: Section 7 (Cloud multi-tenancy pipeline).
 *
 * Two-level authorization model (Phase 18.1):
 *
 * 1. Infrastructure auth: InfluxDBConfig_t.api_token — the HTTP-level
 *    "Authorization: Token <api_token>" header. In production, this is
 *    a service-account token with read/write access to all
 *    "telemetry-{tenant_id}" buckets. Configured once at init.
 *
 * 2. Tenant authorization: TenantToken_t* on every write/query call.
 *    The caller CANNOT specify a bucket name — the target bucket is
 *    derived internally from token->bound_tenant_id. This enforces
 *    that the application layer can only access its authorized tenant's
 *    data, even though the infrastructure token has broader access.
 *
 * This mirrors telemetry_store_query()'s pattern (Phase 8.1 hardening):
 * the token's bound_tenant_id determines scope, not any caller-supplied
 * string.
 */

#ifndef INFLUXDB_CLIENT_H
#define INFLUXDB_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Include TenantToken_t from the cloud telemetry store. */
#include "../../cloud/telemetry_store.h"

/* ---------- Constants ---------- */

#define INFLUXDB_MAX_URL_LEN        256
#define INFLUXDB_MAX_TOKEN_LEN      128
#define INFLUXDB_MAX_ORG_LEN        64
#define INFLUXDB_MAX_BUCKET_LEN     64
#define INFLUXDB_MAX_LINE_LEN       512
#define INFLUXDB_MAX_RESPONSE_LEN   4096
#define INFLUXDB_MAX_QUERY_LEN      1024

/* ---------- Types ---------- */

/**
 * HTTP transport backend — abstracts the actual HTTP call.
 * Real implementation uses curl or esp_http_client;
 * tests provide a mock that captures requests without networking.
 */
typedef struct {
    /**
     * Perform an HTTP request.
     *
     * @param url       Full URL (e.g. http://localhost:8086/api/v2/write?org=...).
     * @param method    "GET" or "POST".
     * @param headers   NULL-terminated array of "Key: Value" header strings.
     * @param body      Request body (NULL for GET).
     * @param body_len  Body length in bytes.
     * @param response  Output buffer for response body.
     * @param resp_len  On input, max response buffer size; on output, actual bytes written.
     * @return true on success (HTTP 2xx), false on error.
     */
    bool (*http_request)(
        const char *url,
        const char *method,
        const char **headers,
        const uint8_t *body,
        size_t body_len,
        uint8_t *response,
        size_t *resp_len
    );
} InfluxDBTransport_t;

/**
 * Configuration for an InfluxDB client instance.
 * Carries the infrastructure-level API token (for HTTP auth).
 * Does NOT carry a bucket — that is derived from TenantToken_t per call.
 */
typedef struct {
    char base_url[INFLUXDB_MAX_URL_LEN];     /**< e.g. "http://localhost:8086". */
    char api_token[INFLUXDB_MAX_TOKEN_LEN];   /**< InfluxDB API token (infrastructure auth). */
    char org[INFLUXDB_MAX_ORG_LEN];           /**< Organization name. */
} InfluxDBConfig_t;

/**
 * A single telemetry data point for InfluxDB write.
 * Note: bucket is NOT caller-controlled — it is derived from the
 * TenantToken_t's bound_tenant_id (Phase 18.1 hardening).
 */
typedef struct {
    char     measurement[64];   /**< Measurement name (e.g. "node_telemetry"). */
    char     tags[8][64];       /**< Up to 8 key=value tag pairs. */
    uint8_t  tag_count;         /**< Number of active tags. */
    char     field_key[64];     /**< Field key (e.g. "value"). */
    float    field_value;       /**< Field value. */
    uint64_t timestamp_ns;      /**< Nanosecond-precision timestamp. */
} InfluxDBPoint_t;

/**
 * Result of InfluxDB operations.
 */
typedef enum {
    INFLUX_OK,
    INFLUX_ERR_PARAM_NULL,
    INFLUX_ERR_TRANSPORT,
    INFLUX_ERR_BUFFER_FULL,
    INFLUX_ERR_LINE_TOO_LONG,
    INFLUX_ERR_QUERY_FAILED,
    INFLUX_ERR_NOT_CONFIGURED,
    INFLUX_ERR_UNAUTHORIZED    /**< Token not authorized for requested tenant. */
} InfluxDBResult_t;

/* ---------- API ---------- */

/**
 * Initialise the InfluxDB client with a configuration and transport.
 *
 * @param config    Client configuration (URL, API token, org).
 * @param transport HTTP transport backend.
 * @return true on success.
 */
bool influxdb_init(const InfluxDBConfig_t *config, const InfluxDBTransport_t *transport);

/**
 * Reset the client to unconfigured state.
 * Useful for testing. Call before influxdb_init() to ensure clean state.
 */
void influxdb_reset(void);

/**
 * Encode a single data point into InfluxDB line protocol format.
 *
 * @param point     Data point to encode.
 * @param line_buf  Output buffer for the line-protocol string.
 * @param buf_size  Size of output buffer.
 * @return Number of bytes written (excluding NUL), or 0 on error.
 */
size_t influxdb_encode_line(const InfluxDBPoint_t *point, char *line_buf, size_t buf_size);

/**
 * Write a single data point to InfluxDB via HTTP.
 * Authorization is enforced: the token's bound_tenant_id determines
 * the target bucket. The caller cannot override this.
 *
 * @param point  Data point to write.
 * @param token  Tenant authorization token (must be valid and non-NULL).
 * @return INFLUX_OK on success, INFLUX_ERR_UNAUTHORIZED if token is invalid.
 */
InfluxDBResult_t influxdb_write(const InfluxDBPoint_t *point, const TenantToken_t *token);

/**
 * Write a batch of data points (newline-separated line protocol).
 * Authorization is enforced via the token.
 *
 * @param points     Array of data points.
 * @param count      Number of points.
 * @param batch_buf  Output buffer for the combined line-protocol payload.
 * @param buf_size   Size of output buffer.
 * @param token      Tenant authorization token (must be valid and non-NULL).
 * @return INFLUX_OK on success.
 */
InfluxDBResult_t influxdb_write_batch(
    const InfluxDBPoint_t *points,
    uint32_t count,
    char *batch_buf,
    size_t buf_size,
    const TenantToken_t *token
);

/**
 * Execute a Flux query and return the raw CSV response.
 * Authorization is enforced: queries are scoped to the token's
 * bound_tenant_id — the caller cannot query cross-tenant data.
 *
 * @param flux_query   Flux query string.
 * @param response     Output buffer for response body.
 * @param resp_len     On input, max buffer size; on output, actual bytes.
 * @param token        Tenant authorization token (must be valid and non-NULL).
 * @return INFLUX_OK on success.
 */
InfluxDBResult_t influxdb_query(
    const char *flux_query,
    uint8_t *response,
    size_t *resp_len,
    const TenantToken_t *token
);

/**
 * Write a TelemetryDataPoint_t directly to InfluxDB.
 * Authorization is enforced via the token — the tenant_id is derived
 * from the token's bound_tenant_id, NOT from the caller.
 *
 * @param token           Tenant authorization token.
 * @param site_id         Site identifier.
 * @param node_id         Node identifier.
 * @param device_class    Device class.
 * @param metric_id       Metric identifier.
 * @param domain_profile_id  Domain profile identifier.
 * @param value           Telemetry value.
 * @param timestamp_ms    Millisecond timestamp (converted to ns internally).
 * @return INFLUX_OK on success.
 */
InfluxDBResult_t influxdb_write_telemetry(
    const TenantToken_t *token,
    const char *site_id,
    const char *node_id,
    const char *device_class,
    const char *metric_id,
    const char *domain_profile_id,
    float value,
    uint64_t timestamp_ms
);

#endif /* INFLUXDB_CLIENT_H */
