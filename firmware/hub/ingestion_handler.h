/**
 * @file ingestion_handler.h
 * @brief Hub-side ingestion handler — production Gateway frame processor.
 *
 * Architecture ref: Section 2 (Tier 3 — Hub), Section 9 (validation gate).
 *
 * This is the REAL (non-test) production code path that:
 *   1. Polls the active radio handler for incoming Gateway frames
 *   2. Calls gateway_ingest_frame() with the Hub's real Edge Node tracker
 *      and Gateway auth tracker
 *   3. For each accepted entry: validates via domain profile's
 *      validateSensorReading(), then evaluates via rule_engine_evaluate()
 *
 * This closes the "zero production callers" gap noted in every prior
 * phase's caller audit (Phases 13-15.2).
 *
 * The handler is designed to run as a periodic task (FreeRTOS) or
 * Arduino loop() call — non-blocking, polls once per invocation.
 *
 * Domain-agnostic: configured at init time via a struct of function
 * pointers and tracker references. No compile-time domain coupling.
 */

#ifndef INGESTION_HANDLER_H
#define INGESTION_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include "../shared/rule_engine_core.h"  /* for DomainProfileVTable_t, SensorReading_t */
#include "../shared/sensor_validation.h" /* for SensorValidationBounds_t, SensorHistory_t */
#include "../gateway/gateway_auth.h"     /* for GatewayGatewayTracker_t, EdgeNodeTracker_t */

/* ---------- Constants ---------- */

#define INGESTION_HANDLER_MAX_ENTRIES  22  /**< Max entries per batch (matches BATCH_MAX_ENTRIES). */

/* ---------- Types ---------- */

/**
 * Radio receive function — abstracts the physical transport.
 * In production: lora_receive() or zigbee_receive() depending on profile.
 * In tests: mock function.
 *
 * @param out_payload  Output: received payload bytes.
 * @param max_len      Size of output buffer.
 * @param out_len      Actual bytes written.
 * @return true if data was available, false otherwise (non-blocking).
 */
typedef bool (*RadioReceiveFunc_t)(uint8_t *out_payload, size_t max_len, size_t *out_len);

/**
 * Validation bounds lookup — abstracts domain-specific bounds.
 * In production: home_get_validation_bounds() or equivalent.
 * In tests: mock function.
 *
 * @param metric_id  The metric to get bounds for.
 * @return Pointer to bounds, or NULL if metric has no bounds configured.
 */
typedef const SensorValidationBounds_t *(*BoundsLookupFunc_t)(uint8_t metric_id);

/**
 * History buffer lookup — abstracts domain-specific history.
 * In production: home_get_metric_history() or equivalent.
 * In tests: mock function.
 *
 * @param metric_id  The metric to get history for.
 * @return Pointer to history buffer, or NULL if metric has no history.
 */
typedef SensorHistory_t *(*HistoryLookupFunc_t)(uint8_t metric_id);

/**
 * Ingestion handler configuration.
 * All pointers must be non-NULL except bounds_lookup and history_lookup
 * (which may be NULL if the domain profile doesn't use sensorValidate()
 * with standalone bounds — e.g. binary/state metrics).
 */
typedef struct {
    GatewayGatewayTracker_t  *gw_tracker;       /**< Hub-side Gateway auth tracker. */
    EdgeNodeTracker_t        *edge_tracker;     /**< Hub-side Edge Node trust tracker. */
    const DomainProfileVTable_t *vtable;        /**< Domain profile vtable (rules + validation + action). */
    RadioReceiveFunc_t        radio_receive;    /**< Radio receive function (LoRa/Zigbee). */
    BoundsLookupFunc_t        bounds_lookup;    /**< Domain bounds lookup (may be NULL). */
    HistoryLookupFunc_t       history_lookup;   /**< Domain history lookup (may be NULL). */
    uint8_t                   domain_profile_id; /**< Domain profile ID for historian (0=Home, 1=Agri, etc.). */
} IngestionHandlerConfig_t;

/**
 * Ingestion handler statistics (for diagnostics/telemetry).
 */
typedef struct {
    uint32_t poll_count;           /**< Total poll cycles. */
    uint32_t frames_received;      /**< Frames received from radio. */
    uint32_t frames_ingested;      /**< Frames accepted by gateway_ingest_frame. */
    uint32_t frames_rejected;      /**< Frames rejected (HMAC, version, edge trust). */
    uint32_t entries_processed;    /**< Total entries processed. */
    uint32_t entries_valid;        /**< Entries that passed sensor validation. */
    uint32_t entries_rejected;     /**< Entries rejected by sensor validation. */
    uint32_t rules_triggered;      /**< Total rules triggered. */
} IngestionHandlerStats_t;

/* ---------- API ---------- */

/**
 * Initialise the ingestion handler.
 *
 * @param config  Handler configuration (caller-allocated, copied).
 * @return true on success.
 */
bool ingestion_handler_init(const IngestionHandlerConfig_t *config);

/**
 * Single poll cycle — non-blocking.
 *
 * Performs one receive → ingest → validate → evaluate cycle:
 *   1. Poll radio for incoming frame
 *   2. Call gateway_ingest_frame() with Hub's trackers
 *   3. For each accepted entry: validate via vtable, evaluate rules
 *
 * Designed to be called periodically from a FreeRTOS task or Arduino loop().
 * Does not block — returns immediately if no data available.
 *
 * @param current_ms  Current monotonic timestamp (ms).
 * @return Number of rules triggered in this cycle (0 if no data or no triggers).
 */
uint8_t ingestion_handler_poll(uint64_t current_ms);

/**
 * Get ingestion handler statistics.
 *
 * @return Pointer to stats (read-only).
 */
const IngestionHandlerStats_t *ingestion_handler_get_stats(void);

/**
 * Reset statistics.
 */
void ingestion_handler_reset_stats(void);

#endif /* INGESTION_HANDLER_H */
