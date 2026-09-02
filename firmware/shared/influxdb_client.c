/**
 * @file influxdb_client.c
 * @brief InfluxDB 2.x line-protocol client implementation.
 *
 * Architecture ref: Section 7 (Cloud multi-tenancy pipeline).
 * Phase 18.1: two-level authorization:
 *   - HTTP auth uses InfluxDBConfig_t.api_token (infrastructure-level).
 *   - Bucket routing uses TenantToken_t.bound_tenant_id (app-level).
 *   - Callers cannot specify arbitrary bucket names.
 *
 * Bucket naming convention: "telemetry-{tenant_id}"
 */

#include "influxdb_client.h"
#include <string.h>
#include <stdio.h>

/* ---------- Internal state ---------- */

static InfluxDBConfig_t s_config;
static const InfluxDBTransport_t *s_transport = NULL;
static bool s_initialised = false;

/* ---------- Tenant-to-InfluxDB mapping ---------- */

/**
 * Derive the InfluxDB bucket name from a tenant ID.
 * Convention: "telemetry-{tenant_id}".
 */
static void derive_bucket(const char *tenant_id, char *bucket, size_t bucket_size)
{
    snprintf(bucket, bucket_size, "telemetry-%s", tenant_id);
}

/**
 * Validate a tenant token. Returns true if the token is valid and
 * has a non-empty bound_tenant_id.
 */
static bool validate_token(const TenantToken_t *token)
{
    if (!token) return false;
    if (!token->valid) return false;
    if (token->bound_tenant_id[0] == '\0') return false;
    return true;
}

/* ---------- Line-protocol encoding ---------- */

/**
 * Escape a tag value for line protocol (commas, spaces only).
 * InfluxDB line protocol only requires escaping commas and spaces in
 * tag values (not equals signs — those are only in tag keys).
 * Writes escaped result into out_buf, returns bytes written.
 */
static size_t escape_tag_value(const char *value, char *out_buf, size_t buf_size)
{
    size_t written = 0;
    for (const char *p = value; *p && written < buf_size - 1; p++) {
        if (*p == ',' || *p == ' ') {
            if (written + 1 < buf_size) {
                out_buf[written++] = '\\';
                out_buf[written++] = *p;
            } else {
                break;
            }
        } else {
            out_buf[written++] = *p;
        }
    }
    out_buf[written] = '\0';
    return written;
}

size_t influxdb_encode_line(const InfluxDBPoint_t *point, char *line_buf, size_t buf_size)
{
    if (!point || !line_buf || buf_size == 0) return 0;

    /* Start with measurement name. */
    size_t pos = 0;
    size_t remaining = buf_size - 1; /* Reserve space for NUL. */

    /* Copy measurement name (escape commas and spaces). */
    for (const char *p = point->measurement; *p && pos < remaining; p++) {
        if (*p == ',' || *p == ' ') {
            line_buf[pos++] = '\\';
        }
        if (pos < remaining) {
            line_buf[pos++] = *p;
        }
    }

    /* Add tags: ,key1=value1,key2=value2 */
    for (int i = 0; i < point->tag_count && i < 8; i++) {
        if (point->tags[i][0] == '\0') continue;

        if (pos < remaining) line_buf[pos++] = ',';

        /* Find '=' in the tag. */
        const char *eq = strchr(point->tags[i], '=');
        if (eq) {
            /* Copy key (up to '='). */
            for (const char *p = point->tags[i]; p < eq && pos < remaining; p++) {
                line_buf[pos++] = *p;
            }
            if (pos < remaining) line_buf[pos++] = '=';
            /* Escape and copy value. */
            char escaped[128];
            escape_tag_value(eq + 1, escaped, sizeof(escaped));
            for (const char *p = escaped; *p && pos < remaining; p++) {
                line_buf[pos++] = *p;
            }
        }
    }

    /* Add space before fields. */
    if (pos < remaining) line_buf[pos++] = ' ';

    /* Add field: key=value */
    for (const char *p = point->field_key; *p && pos < remaining; p++) {
        line_buf[pos++] = *p;
    }
    if (pos < remaining) line_buf[pos++] = '=';
    pos += snprintf(line_buf + pos, remaining - pos, "%g", (double)point->field_value);

    /* Add timestamp (nanoseconds). */
    if (point->timestamp_ns > 0) {
        if (pos < remaining) line_buf[pos++] = ' ';
        pos += snprintf(line_buf + pos, remaining - pos, "%llu",
                        (unsigned long long)point->timestamp_ns);
    }

    line_buf[pos] = '\0';
    return pos;
}

/* ---------- Transport-dependent operations ---------- */

/**
 * Build write URL with tenant-scoped bucket.
 * Bucket is derived from TenantToken_t — caller cannot override.
 */
static bool make_write_url(char *url, size_t url_size, const TenantToken_t *token)
{
    if (!s_config.base_url[0]) return false;

    char bucket[INFLUXDB_MAX_BUCKET_LEN];
    derive_bucket(token->bound_tenant_id, bucket, sizeof(bucket));

    int n = snprintf(url, url_size,
                      "%s/api/v2/write?org=%s&bucket=%s&precision=ns",
                      s_config.base_url, s_config.org, bucket);
    return (n > 0 && (size_t)n < url_size);
}

/**
 * Build the Authorization header using the configured infrastructure API token.
 */
static void make_auth_header(char *header, size_t header_size)
{
    snprintf(header, header_size, "Authorization: Token %s", s_config.api_token);
}

/**
 * Build query URL (org-scoped, queries are Flux-filtered by tenant).
 */
static bool make_query_url(char *url, size_t url_size)
{
    if (!s_config.base_url[0]) return false;
    int n = snprintf(url, url_size,
                      "%s/api/v2/query?org=%s",
                      s_config.base_url, s_config.org);
    return (n > 0 && (size_t)n < url_size);
}

InfluxDBResult_t influxdb_write(const InfluxDBPoint_t *point, const TenantToken_t *token)
{
    if (!s_initialised || !s_transport) return INFLUX_ERR_NOT_CONFIGURED;
    if (!point || !token) return INFLUX_ERR_PARAM_NULL;

    /* Authorization: token must be valid. */
    if (!validate_token(token)) return INFLUX_ERR_UNAUTHORIZED;

    char line[INFLUXDB_MAX_LINE_LEN];
    size_t line_len = influxdb_encode_line(point, line, sizeof(line));
    if (line_len == 0) return INFLUX_ERR_LINE_TOO_LONG;

    char url[INFLUXDB_MAX_URL_LEN];
    if (!make_write_url(url, sizeof(url), token)) return INFLUX_ERR_BUFFER_FULL;

    char auth_header[INFLUXDB_MAX_TOKEN_LEN + 32];
    make_auth_header(auth_header, sizeof(auth_header));

    const char *headers[] = {
        auth_header,
        "Content-Type: text/plain; charset=utf-8",
        NULL
    };

    uint8_t response[64];
    size_t resp_len = sizeof(response);
    bool ok = s_transport->http_request(url, "POST", headers,
                                         (const uint8_t *)line, line_len,
                                         response, &resp_len);
    return ok ? INFLUX_OK : INFLUX_ERR_TRANSPORT;
}

InfluxDBResult_t influxdb_write_batch(
    const InfluxDBPoint_t *points,
    uint32_t count,
    char *batch_buf,
    size_t buf_size,
    const TenantToken_t *token
)
{
    if (!s_initialised || !s_transport) return INFLUX_ERR_NOT_CONFIGURED;
    if (!points || !batch_buf || count == 0) return INFLUX_ERR_PARAM_NULL;
    if (!token) return INFLUX_ERR_PARAM_NULL;

    /* Authorization: token must be valid. */
    if (!validate_token(token)) return INFLUX_ERR_UNAUTHORIZED;

    size_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        size_t line_len = influxdb_encode_line(&points[i],
                                               batch_buf + total,
                                               buf_size - total - 1);
        if (line_len == 0) return INFLUX_ERR_BUFFER_FULL;
        total += line_len;
        if (total < buf_size - 1) {
            batch_buf[total++] = '\n';
        }
    }
    /* Remove trailing newline. */
    if (total > 0 && batch_buf[total - 1] == '\n') {
        total--;
    }
    batch_buf[total] = '\0';

    char url[INFLUXDB_MAX_URL_LEN];
    if (!make_write_url(url, sizeof(url), token)) return INFLUX_ERR_BUFFER_FULL;

    char auth_header[INFLUXDB_MAX_TOKEN_LEN + 32];
    make_auth_header(auth_header, sizeof(auth_header));

    const char *headers[] = {
        auth_header,
        "Content-Type: text/plain; charset=utf-8",
        NULL
    };

    uint8_t response[64];
    size_t resp_len = sizeof(response);
    bool ok = s_transport->http_request(url, "POST", headers,
                                         (const uint8_t *)batch_buf, total,
                                         response, &resp_len);
    return ok ? INFLUX_OK : INFLUX_ERR_TRANSPORT;
}

InfluxDBResult_t influxdb_query(
    const char *flux_query,
    uint8_t *response,
    size_t *resp_len,
    const TenantToken_t *token
)
{
    if (!s_initialised || !s_transport) return INFLUX_ERR_NOT_CONFIGURED;
    if (!flux_query || !response || !resp_len) return INFLUX_ERR_PARAM_NULL;
    if (!token) return INFLUX_ERR_PARAM_NULL;

    /* Authorization: token must be valid. */
    if (!validate_token(token)) return INFLUX_ERR_UNAUTHORIZED;

    char url[INFLUXDB_MAX_URL_LEN];
    if (!make_query_url(url, sizeof(url))) return INFLUX_ERR_BUFFER_FULL;

    char auth_header[INFLUXDB_MAX_TOKEN_LEN + 32];
    make_auth_header(auth_header, sizeof(auth_header));

    const char *headers[] = {
        auth_header,
        "Content-Type: application/vnd.flux",
        "Accept: application/csv",
        NULL
    };

    bool ok = s_transport->http_request(url, "POST", headers,
                                         (const uint8_t *)flux_query,
                                         strlen(flux_query),
                                         response, resp_len);
    return ok ? INFLUX_OK : INFLUX_ERR_QUERY_FAILED;
}

InfluxDBResult_t influxdb_write_telemetry(
    const TenantToken_t *token,
    const char *site_id,
    const char *node_id,
    const char *device_class,
    const char *metric_id,
    const char *domain_profile_id,
    float value,
    uint64_t timestamp_ms
)
{
    /* Authorization: token must be valid. */
    if (!validate_token(token)) return INFLUX_ERR_UNAUTHORIZED;
    if (!node_id || !metric_id) return INFLUX_ERR_PARAM_NULL;

    /* Use the token's bound tenant — NOT any caller-supplied string. */
    const char *tenant_id = token->bound_tenant_id;

    InfluxDBPoint_t point;
    memset(&point, 0, sizeof(point));

    strncpy(point.measurement, "node_telemetry", sizeof(point.measurement) - 1);
    point.tag_count = 0;

    /* Build tag pairs: tenant_id=X, site_id=X, node_id=X, etc. */
    if (tenant_id[0]) {
        snprintf(point.tags[point.tag_count], sizeof(point.tags[0]),
                 "tenant_id=%.*s", (int)(sizeof(point.tags[0]) - 11), tenant_id);
        point.tag_count++;
    }
    if (site_id && site_id[0]) {
        snprintf(point.tags[point.tag_count], sizeof(point.tags[0]),
                 "site_id=%s", site_id);
        point.tag_count++;
    }
    if (node_id && node_id[0]) {
        snprintf(point.tags[point.tag_count], sizeof(point.tags[0]),
                 "node_id=%s", node_id);
        point.tag_count++;
    }
    if (device_class && device_class[0]) {
        snprintf(point.tags[point.tag_count], sizeof(point.tags[0]),
                 "device_class=%s", device_class);
        point.tag_count++;
    }
    if (metric_id && metric_id[0]) {
        snprintf(point.tags[point.tag_count], sizeof(point.tags[0]),
                 "metric_id=%s", metric_id);
        point.tag_count++;
    }
    if (domain_profile_id && domain_profile_id[0]) {
        snprintf(point.tags[point.tag_count], sizeof(point.tags[0]),
                 "domain_profile_id=%s", domain_profile_id);
        point.tag_count++;
    }

    strncpy(point.field_key, "value", sizeof(point.field_key) - 1);
    point.field_value = value;
    point.timestamp_ns = timestamp_ms * 1000000ULL;

    return influxdb_write(&point, token);
}

/* ---------- Init ---------- */

void influxdb_reset(void)
{
    memset(&s_config, 0, sizeof(s_config));
    s_transport = NULL;
    s_initialised = false;
}

bool influxdb_init(const InfluxDBConfig_t *config, const InfluxDBTransport_t *transport)
{
    influxdb_reset();
    if (!config || !transport) return false;

    memcpy(&s_config, config, sizeof(InfluxDBConfig_t));
    s_transport = transport;
    s_initialised = true;
    return true;
}
