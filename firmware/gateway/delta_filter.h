/**
 * @file delta_filter.h
 * @brief Delta filter — suppresses below-threshold sensor changes.
 *
 * Architecture ref: Section 2 (Tier 2 — Gateway, fog computing).
 *                   "Only forward changed values" = delta filtering.
 *
 * Domain-agnostic: each metric has a configurable per-metric delta
 * threshold. A new reading is forwarded (marked dirty in the cache)
 * only if |new_value - cached_value| > threshold. This is the core
 * bandwidth-saving mechanism that makes LoRa/Zigbee airtime viable
 * at scale (Section 3 reasoning).
 *
 * This module sits BETWEEN the RS-485 frame parser (Edge→Gateway)
 * and the stateful cache (Gateway→Hub forwarding).
 */

#ifndef DELTA_FILTER_H
#define DELTA_FILTER_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Constants ---------- */

#define DELTA_FILTER_MAX_METRICS  16  /**< Max metrics with configured thresholds. */

/* ---------- Types ---------- */

/**
 * Result of delta filter evaluation.
 */
typedef enum {
    DELTA_FORWARD,      /**< Change exceeds threshold — forward to cache. */
    DELTA_SUPPRESS,     /**< Change below threshold — do not forward. */
    DELTA_FIRST_READ,   /**< No previous value — always forward (first read). */
    DELTA_ERR_NOT_FOUND /**< No threshold configured for this metric. */
} DeltaFilterResult_t;

/**
 * Per-metric delta threshold configuration.
 */
typedef struct {
    uint8_t  metric_id;      /**< Metric identifier. */
    float    abs_threshold;  /**< Absolute delta threshold (|Δv| > this → forward). */
    bool     configured;     /**< Whether this slot has a threshold set. */
} DeltaThreshold_t;

/* ---------- API ---------- */

/**
 * Initialise the delta filter. Clears all thresholds.
 */
void delta_filter_init(void);

/**
 * Configure a delta threshold for a specific metric.
 *
 * @param metric_id      Metric identifier.
 * @param abs_threshold  Absolute threshold. A reading is forwarded only if
 *                       |new_value - cached_value| > abs_threshold.
 * @return true on success, false if threshold table is full.
 */
bool delta_filter_set_threshold(uint8_t metric_id, float abs_threshold);

/**
 * Evaluate whether a new reading should be forwarded.
 *
 * Looks up the cached value for (node_id, metric_id), compares against
 * the new reading, and returns the appropriate action.
 *
 * @param node_id    Edge Node identifier.
 * @param metric_id  Metric identifier.
 * @param new_value  The new sensor reading.
 * @return DELTA_FORWARD if the change exceeds threshold (or first read),
 *         DELTA_SUPPRESS if below threshold,
 *         DELTA_ERR_NOT_FOUND if no threshold is configured for this metric.
 */
DeltaFilterResult_t delta_filter_evaluate(
    uint16_t node_id, uint8_t metric_id, float new_value);

/**
 * Get the number of configured thresholds.
 */
uint8_t delta_filter_config_count(void);

#endif /* DELTA_FILTER_H */
