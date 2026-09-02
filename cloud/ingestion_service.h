/**
 * @file ingestion_service.h
 * @brief Cloud ingestion service — MQTT subscriber + telemetry writer.
 *
 * Architecture ref: Section 7 (Cloud multi-tenancy pipeline).
 *
 * This is a native/Linux-side component (not embedded firmware).
 * It subscribes to the MQTT topic namespace, extracts tenant/site/device
 * from topic paths, and writes to the time-series store.
 *
 * In production, this would run as a cloud service with real MQTT broker
 * and InfluxDB. For testing, it uses mock components.
 */

#ifndef INGESTION_SERVICE_H
#define INGESTION_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "telemetry_store.h"

/* ---------- Constants ---------- */

#define INGESTION_MAX_SUBSCRIBERS  8    /**< Max concurrent topic subscribers. */
#define INGESTION_MAX_TOPIC_LEN    128  /**< Max topic string length. */
#define INGESTION_DEFAULT_CARDINALITY_THRESHOLD 10000  /**< Default cardinality warning threshold. */

/* ---------- Types ---------- */

/**
 * Ingestion service configuration.
 */
typedef struct {
    uint32_t cardinality_warning_threshold; /**< Series count that triggers warning. 0 = disabled. */
} IngestionConfig_t;

/**
 * Telemetry message parsed from an MQTT topic + payload.
 */
typedef struct {
    char     tenant_id[TELEMETRY_STORE_MAX_TAG_LEN];
    char     site_id[TELEMETRY_STORE_MAX_TAG_LEN];
    char     device_id[TELEMETRY_STORE_MAX_TAG_LEN];
    char     metric_id[TELEMETRY_STORE_MAX_TAG_LEN];
    char     domain_profile_id[TELEMETRY_STORE_MAX_TAG_LEN];
    float    value;
    uint64_t timestamp_ms;
} IngestionTelemetry_t;

/**
 * Result of ingestion operations.
 */
typedef enum {
    INGEST_OK,
    INGEST_ERR_PARAM_NULL,
    INGEST_ERR_TOPIC_PARSE,       /**< Could not extract IDs from topic. */
    INGEST_ERR_STORE_FULL,
    INGEST_ERR_NOT_INIT
} IngestionResult_t;

/**
 * Cardinality warning callback.
 * Called when a tenant's series count crosses the threshold.
 */
typedef void (*cardinality_warning_callback_t)(
    const char *tenant_id,
    uint32_t current_count,
    uint32_t threshold
);

/* ---------- API ---------- */

/**
 * Initialise the ingestion service.
 *
 * @param config  Configuration (NULL for defaults).
 * @return true on success.
 */
bool ingestion_service_init(const IngestionConfig_t *config);

/**
 * Parse an MQTT topic string and extract tenant/site/device IDs.
 *
 * Expected format: "rainmaker/{tenant_id}/{site_id}/{device_id}/telemetry"
 *
 * @param topic        Topic string to parse.
 * @param out_tenant   Output: tenant_id (must be >= TELEMETRY_STORE_MAX_TAG_LEN).
 * @param out_site     Output: site_id.
 * @param out_device   Output: device_id.
 * @return INGEST_OK on success.
 */
IngestionResult_t ingestion_parse_topic(
    const char *topic,
    char *out_tenant,
    char *out_site,
    char *out_device
);

/**
 * Ingest a telemetry message from an MQTT topic + payload.
 * Parses the topic, extracts IDs, writes to the store, and checks
 * cardinality.
 *
 * @param topic        MQTT topic string.
 * @param payload      JSON-like payload (simplified: "metric_id:value").
 * @param payload_len  Payload length.
 * @param timestamp_ms Timestamp in milliseconds.
 * @return INGEST_OK on success.
 */
IngestionResult_t ingestion_ingest(
    const char *topic,
    const char *payload,
    uint16_t payload_len,
    uint64_t timestamp_ms
);

/**
 * Register a cardinality warning callback.
 *
 * @param callback  Callback function (NULL to disable).
 */
void ingestion_set_cardinality_callback(cardinality_warning_callback_t callback);

/**
 * Get the ingestion configuration.
 *
 * @return Pointer to current config (read-only).
 */
const IngestionConfig_t *ingestion_get_config(void);

#endif /* INGESTION_SERVICE_H */
