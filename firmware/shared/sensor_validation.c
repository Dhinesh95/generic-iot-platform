/**
 * @file sensor_validation.c
 * @brief Sensor validation / plausibility gate implementation.
 *
 * Architecture ref: Section 9.
 *
 * Placement diagram from Section 9:
 *   sensorPollTask (1000ms) → raw reading
 *       → sensorValidate() [gate]
 *           → VALID   → safetyMonitorTask / motorControlTask
 *           → INVALID → audit log + domain fail-safe trigger
 *
 * Validation failure is NEVER allowed to pass a stale/invalid
 * reading to the rule engine. The reading is treated as unknown/unsafe.
 */

#include "sensor_validation.h"
#include <string.h>

/* ---------- Internal state ---------- */

static bool g_initialised = false;

/* ---------- Public API ---------- */

bool sensor_validation_init(void)
{
    g_initialised = true;
    return true;
}

SensorValidationResult_t sensorValidate(
    uint8_t node_id,
    uint8_t metric_id,
    float new_value,
    const SensorValidationBounds_t *bounds,
    SensorHistory_t *history,
    uint64_t current_ms)
{
    if (!bounds || !history) return SENSOR_OUT_OF_PHYSICAL_RANGE;
    if (!g_initialised) return SENSOR_OUT_OF_PHYSICAL_RANGE;
    (void)node_id;
    (void)metric_id;

    /* Check 1: Physical range. */
    if (new_value < bounds->min_physical || new_value > bounds->max_physical) {
        return SENSOR_OUT_OF_PHYSICAL_RANGE;
    }

    /* If history is not yet populated, we can only do range checks. */
    if (!history->initialised || history->count < 1) {
        return SENSOR_VALID;
    }

    /* Check 2: Rate of change. */
    if (bounds->max_rate_of_change > 0.0f && history->count > 0) {
        /* Find the most recent entry. */
        uint8_t latest_idx;
        if (history->count >= SENSOR_HISTORY_SIZE) {
            latest_idx = history->write_index;  /* write_index wraps to 0 */
        } else {
            latest_idx = history->count - 1;
        }

        float prev_value = history->values[latest_idx];
        uint64_t prev_ts = history->timestamps_ms[latest_idx];

        if (current_ms > prev_ts && prev_ts > 0) {
            float dt_sec = (float)(current_ms - prev_ts) / 1000.0f;
            if (dt_sec > 0.0f) {
                float delta = new_value - prev_value;
                if (delta < 0.0f) delta = -delta;
                float rate = delta / dt_sec;
                if (rate > bounds->max_rate_of_change) {
                    return SENSOR_RATE_EXCEEDED;
                }
            }
        }
    }

    /* Check 3: Stuck detection. */
    if (bounds->stuck_timeout_sec > 0 && history->count > 0) {
        if (new_value == history->last_value) {
            /* Value unchanged — check how long it has been stuck. */
            if (history->last_change_ms > 0 && current_ms > history->last_change_ms) {
                uint64_t stuck_ms = current_ms - history->last_change_ms;
                uint64_t timeout_ms = (uint64_t)bounds->stuck_timeout_sec * 1000ULL;
                if (stuck_ms > timeout_ms) {
                    return SENSOR_STUCK;
                }
            }
        }
    }

    /* All checks passed. */
    return SENSOR_VALID;
}

void sensor_history_record(
    SensorHistory_t *history,
    uint8_t node_id,
    uint8_t metric_id,
    float value,
    uint64_t timestamp_ms)
{
    if (!history) return;

    if (!history->initialised) {
        memset(history, 0, sizeof(SensorHistory_t));
        history->node_id = node_id;
        history->metric_id = metric_id;
        history->initialised = true;
    }

    /* Track value changes for stuck detection. */
    if (value != history->last_value) {
        history->last_change_ms = timestamp_ms;
    }
    history->last_value = value;

    /* Write to circular buffer. */
    history->values[history->write_index] = value;
    history->timestamps_ms[history->write_index] = timestamp_ms;

    history->write_index = (history->write_index + 1) % SENSOR_HISTORY_SIZE;
    if (history->count < SENSOR_HISTORY_SIZE) {
        history->count++;
    }
}

void sensor_history_reset(SensorHistory_t *history)
{
    if (history) {
        memset(history, 0, sizeof(SensorHistory_t));
    }
}
