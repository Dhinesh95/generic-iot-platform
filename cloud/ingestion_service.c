/**
 * @file ingestion_service.c
 * @brief Cloud ingestion service implementation.
 *
 * Architecture ref: Section 7 (Cloud multi-tenancy pipeline).
 *
 * Subscribes to MQTT topics, parses tenant/site/device from topic path,
 * writes to the telemetry store, and fires cardinality warnings.
 */

#include "ingestion_service.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ---------- Internal state ---------- */

static IngestionConfig_t s_config;
static bool s_initialised = false;
static cardinality_warning_callback_t s_cardinality_callback = NULL;

/* ---------- Internal helpers ---------- */

/**
 * Extract the Nth path component from a topic string.
 * E.g. for "a/b/c/d" with index=1, returns "b".
 */
static bool extract_component(const char *topic, int index,
                               char *out, size_t out_size)
{
    if (!topic || !out || out_size == 0) return false;

    const char *p = topic;
    int current = 0;

    /* Skip leading separator. */
    if (*p == '/') p++;

    while (*p && current < index) {
        if (*p == '/') current++;
        p++;
    }

    if (current != index) return false;

    /* Copy until next separator or end. */
    size_t i = 0;
    while (*p && *p != '/' && i < out_size - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';

    return (i > 0);
}

/* ---------- Public API ---------- */

bool ingestion_service_init(const IngestionConfig_t *config)
{
    if (config) {
        memcpy(&s_config, config, sizeof(IngestionConfig_t));
    } else {
        s_config.cardinality_warning_threshold = INGESTION_DEFAULT_CARDINALITY_THRESHOLD;
    }
    s_cardinality_callback = NULL;
    s_initialised = true;
    return true;
}

IngestionResult_t ingestion_parse_topic(
    const char *topic,
    char *out_tenant,
    char *out_site,
    char *out_device)
{
    if (!topic || !out_tenant || !out_site || !out_device)
        return INGEST_ERR_PARAM_NULL;

    /* Expected: "rainmaker/{tenant_id}/{site_id}/{device_id}/telemetry" */
    /* Component 0 = "rainmaker", 1 = tenant, 2 = site, 3 = device, 4 = "telemetry" */

    char prefix[16];
    if (!extract_component(topic, 0, prefix, sizeof(prefix))) return INGEST_ERR_TOPIC_PARSE;
    if (strcmp(prefix, "rainmaker") != 0) return INGEST_ERR_TOPIC_PARSE;

    if (!extract_component(topic, 1, out_tenant, TELEMETRY_STORE_MAX_TAG_LEN))
        return INGEST_ERR_TOPIC_PARSE;
    if (out_tenant[0] == '\0') return INGEST_ERR_TOPIC_PARSE;

    if (!extract_component(topic, 2, out_site, TELEMETRY_STORE_MAX_TAG_LEN))
        return INGEST_ERR_TOPIC_PARSE;
    if (out_site[0] == '\0') return INGEST_ERR_TOPIC_PARSE;

    if (!extract_component(topic, 3, out_device, TELEMETRY_STORE_MAX_TAG_LEN))
        return INGEST_ERR_TOPIC_PARSE;
    if (out_device[0] == '\0') return INGEST_ERR_TOPIC_PARSE;

    char suffix[16];
    if (!extract_component(topic, 4, suffix, sizeof(suffix))) return INGEST_ERR_TOPIC_PARSE;
    if (strcmp(suffix, "telemetry") != 0) return INGEST_ERR_TOPIC_PARSE;

    return INGEST_OK;
}

IngestionResult_t ingestion_ingest(
    const char *topic,
    const char *payload,
    uint16_t payload_len,
    uint64_t timestamp_ms)
{
    if (!s_initialised) return INGEST_ERR_NOT_INIT;
    if (!topic || !payload) return INGEST_ERR_PARAM_NULL;
    (void)payload_len;

    /* Parse topic to extract IDs. */
    char tenant_id[TELEMETRY_STORE_MAX_TAG_LEN];
    char site_id[TELEMETRY_STORE_MAX_TAG_LEN];
    char device_id[TELEMETRY_STORE_MAX_TAG_LEN];

    IngestionResult_t parse_result = ingestion_parse_topic(
        topic, tenant_id, site_id, device_id);
    if (parse_result != INGEST_OK) return parse_result;

    /* Parse simplified payload: "metric_id:value" */
    char metric_id[TELEMETRY_STORE_MAX_TAG_LEN] = "unknown";
    float value = 0.0f;

    const char *colon = strchr(payload, ':');
    if (colon) {
        size_t metric_len = (size_t)(colon - payload);
        if (metric_len >= TELEMETRY_STORE_MAX_TAG_LEN) metric_len = TELEMETRY_STORE_MAX_TAG_LEN - 1;
        memcpy(metric_id, payload, metric_len);
        metric_id[metric_len] = '\0';
        value = strtof(colon + 1, NULL);
    }

    /* Write to telemetry store. */
    TelemetryDataPoint_t point;
    memset(&point, 0, sizeof(point));
    strncpy(point.tenant_id, tenant_id, TELEMETRY_STORE_MAX_TAG_LEN - 1);
    strncpy(point.site_id, site_id, TELEMETRY_STORE_MAX_TAG_LEN - 1);
    strncpy(point.node_id, device_id, TELEMETRY_STORE_MAX_TAG_LEN - 1);
    strncpy(point.metric_id, metric_id, TELEMETRY_STORE_MAX_TAG_LEN - 1);
    strncpy(point.domain_profile_id, "unknown", TELEMETRY_STORE_MAX_TAG_LEN - 1);
    point.value = value;
    point.timestamp_ms = timestamp_ms;

    TelemetryStoreResult_t store_result = telemetry_store_write(&point);
    if (store_result == STORE_ERR_FULL) return INGEST_ERR_STORE_FULL;

    /* Check cardinality warning. */
    if (s_config.cardinality_warning_threshold > 0 && s_cardinality_callback) {
        uint32_t cardinality = telemetry_store_get_cardinality(tenant_id);
        if (cardinality > s_config.cardinality_warning_threshold) {
            s_cardinality_callback(tenant_id, cardinality,
                                   s_config.cardinality_warning_threshold);
        }
    }

    return INGEST_OK;
}

void ingestion_set_cardinality_callback(cardinality_warning_callback_t callback)
{
    s_cardinality_callback = callback;
}

const IngestionConfig_t *ingestion_get_config(void)
{
    return s_initialised ? &s_config : NULL;
}
