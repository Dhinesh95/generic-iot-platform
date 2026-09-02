/**
 * @file sensor_validation.h
 * @brief Sensor validation / plausibility gate.
 *
 * Architecture ref: Section 9.
 *
 * Validation runs as a gate between raw sensor polling and rule
 * evaluation. On failure, the reading is treated as unknown/unsafe
 * and routed to the domain's fail-safe mode — it is NEVER passed
 * to the rule engine as a valid reading.
 *
 * Bounds (physical range, rate of change, stuck timeout) are
 * domain-specific data supplied through the domain profile.
 */

#ifndef SENSOR_VALIDATION_H
#define SENSOR_VALIDATION_H

#include <stdint.h>
#include <stdbool.h>
#include "rule_engine_core.h"  /* for SensorReading_t */

/* ---------- Constants ---------- */

#define SENSOR_HISTORY_SIZE   16   /**< Rolling history depth per metric. */
#define SENSOR_MAX_METRICS    16   /**< Maximum tracked metric IDs. */

/* ---------- Types ---------- */

/**
 * Plausibility bounds for a sensor metric.
 * These are domain-specific data, not code — supplied by the
 * domain profile.
 */
typedef struct {
    float    min_physical;          /**< Lowest physically possible value. */
    float    max_physical;          /**< Highest physically possible value. */
    float    max_rate_of_change;    /**< Maximum allowed change per second. */
    uint16_t stuck_timeout_sec;     /**< Seconds before a constant value is suspect. */
    uint8_t  cross_check_node_id;   /**< Redundant sensor to correlate with; 0 = none. */
} SensorValidationBounds_t;

/**
 * Result of sensor validation.
 */
typedef enum {
    SENSOR_VALID,                   /**< Reading is within all bounds. */
    SENSOR_OUT_OF_PHYSICAL_RANGE,   /**< Value outside [min_physical, max_physical]. */
    SENSOR_RATE_EXCEEDED,           /**< Rate of change exceeds max_rate_of_change. */
    SENSOR_STUCK,                   /**< Value unchanged beyond stuck_timeout_sec. */
    SENSOR_CROSS_CHECK_MISMATCH     /**< Cross-check sensor disagrees. */
} SensorValidationResult_t;

/**
 * Rolling history buffer for a single metric on a single node.
 * Managed by the core, not by domain profiles.
 */
typedef struct {
    uint8_t  node_id;
    uint8_t  metric_id;
    float    values[SENSOR_HISTORY_SIZE];
    uint64_t timestamps_ms[SENSOR_HISTORY_SIZE];
    uint8_t  write_index;            /**< Next write position (circular). */
    uint8_t  count;                  /**< Number of valid entries (up to SENSOR_HISTORY_SIZE). */
    float    last_value;             /**< Most recent value (for stuck detection). */
    uint64_t last_change_ms;         /**< Timestamp of last value change. */
    bool     initialised;
} SensorHistory_t;

/* ---------- API ---------- */

/**
 * Initialise the sensor validation subsystem.
 *
 * @return true on success.
 */
bool sensor_validation_init(void);

/**
 * Validate a sensor reading against domain-provided bounds.
 *
 * This function MUST be called as a gate BEFORE rule evaluation:
 *   sensorPollTask → raw reading → sensorValidate() → VALID → rule engine
 *                                                   → INVALID → fail-safe
 *
 * @param node_id    Source node ID.
 * @param metric_id  Metric identifier.
 * @param new_value  The raw sensor reading.
 * @param bounds     Domain-specific plausibility bounds.
 * @param history    Rolling history buffer for this metric (caller-managed).
 * @param current_ms Current monotonic timestamp in milliseconds.
 * @return SENSOR_VALID if the reading passes all checks.
 */
SensorValidationResult_t sensorValidate(
    uint8_t node_id,
    uint8_t metric_id,
    float new_value,
    const SensorValidationBounds_t *bounds,
    SensorHistory_t *history,
    uint64_t current_ms
);

/**
 * Record a validated reading into the history buffer.
 * Called after sensorValidate() returns SENSOR_VALID.
 *
 * @param history    The history buffer to update.
 * @param node_id    Source node ID.
 * @param metric_id  Metric identifier.
 * @param value      The validated value.
 * @param timestamp_ms  Timestamp of the reading.
 */
void sensor_history_record(
    SensorHistory_t *history,
    uint8_t node_id,
    uint8_t metric_id,
    float value,
    uint64_t timestamp_ms
);

/**
 * Reset a history buffer (e.g. after sensor recalibration).
 *
 * @param history  History buffer to reset.
 */
void sensor_history_reset(SensorHistory_t *history);

#endif /* SENSOR_VALIDATION_H */
