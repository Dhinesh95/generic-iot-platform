/**
 * @file hvac_validation_bounds.c
 * @brief HVAC sensor plausibility bounds.
 *
 * Architecture ref: Section 9, Bounds (Domain Profile, Data Not Code).
 *
 * HVAC bounds are tighter than Agriculture's outdoor range (-40 to +60°C)
 * because indoor environments have smaller temperature swings. These are
 * illustrative defaults — real values require field calibration.
 */

#include "hvac_validation_bounds.h"
#include <string.h>

/* ---------- Validation bounds for HVAC metrics ---------- */

/**
 * Supply air temperature sensor (analog: 0-60 C).
 *
 * Physical range: 0.0 to 60.0 °C.
 * Rate of change: max 2.0°C/sec (supply air changes gradually with
 *   compressor cycling; rapid spikes indicate sensor noise).
 * Stuck timeout: 600 sec (10 minutes — supply temp should vary
 *   with compressor cycles; stuck for 10 min is suspect).
 */
static const SensorValidationBounds_t s_supply_temp_bounds = {
    .min_physical        = 0.0f,
    .max_physical        = 60.0f,
    .max_rate_of_change  = 2.0f,
    .stuck_timeout_sec   = 600,   /* 10 minutes. */
    .cross_check_node_id = 0
};

/**
 * Return air temperature sensor (analog: 0-60 C).
 *
 * Physical range: 0.0 to 60.0 °C.
 * Rate of change: max 1.0°C/sec (return air changes slowly —
 *   rapid changes indicate sensor malfunction or door open).
 * Stuck timeout: 900 sec (15 minutes — return air should drift
 *   with HVAC cycling and occupancy; stuck for 15 min is suspect).
 */
static const SensorValidationBounds_t s_return_temp_bounds = {
    .min_physical        = 0.0f,
    .max_physical        = 60.0f,
    .max_rate_of_change  = 1.0f,
    .stuck_timeout_sec   = 900,   /* 15 minutes. */
    .cross_check_node_id = 0
};

/**
 * Indoor humidity sensor (analog: 0-100%).
 *
 * Physical range: 0.0 to 100.0 percent.
 * Rate of change: max 5.0%/sec (indoor humidity changes slowly —
 *   rapid changes indicate sensor malfunction).
 * Stuck timeout: 1800 sec (30 minutes — indoor humidity should vary
 *   with HVAC dehumidification; stuck for 30 min is suspect).
 */
static const SensorValidationBounds_t s_indoor_humidity_bounds = {
    .min_physical        = 0.0f,
    .max_physical        = 100.0f,
    .max_rate_of_change  = 5.0f,
    .stuck_timeout_sec   = 1800,  /* 30 minutes. */
    .cross_check_node_id = 0
};

/**
 * Ambient (indoor) temperature sensor (analog: -10 to +50 C).
 *
 * Physical range: -10.0 to +50.0 °C (indoor range, tighter than
 *   Agriculture's -40 to +60°C outdoor range).
 * Rate of change: max 1.0°C/sec (indoor temp changes slowly —
 *   rapid changes indicate sensor malfunction or HVAC failure).
 * Stuck timeout: 1800 sec (30 minutes — indoor temp should vary
 *   with HVAC cycling; stuck for 30 min is suspect).
 */
static const SensorValidationBounds_t s_ambient_temp_bounds = {
    .min_physical        = -10.0f,
    .max_physical        = 50.0f,
    .max_rate_of_change  = 1.0f,
    .stuck_timeout_sec   = 1800,  /* 30 minutes. */
    .cross_check_node_id = 0
};

/* ---------- History buffers ---------- */

static SensorHistory_t s_supply_temp_history;
static SensorHistory_t s_return_temp_history;
static SensorHistory_t s_indoor_humidity_history;
static SensorHistory_t s_ambient_temp_history;

/* ---------- Public API ---------- */

const SensorValidationBounds_t *hvac_get_validation_bounds(uint8_t metric_id)
{
    switch (metric_id) {
        case HVAC_METRIC_SUPPLY_TEMP:
            return &s_supply_temp_bounds;
        case HVAC_METRIC_RETURN_TEMP:
            return &s_return_temp_bounds;
        case HVAC_METRIC_INDOOR_HUMIDITY:
            return &s_indoor_humidity_bounds;
        case HVAC_METRIC_AMBIENT_TEMP:
            return &s_ambient_temp_bounds;
        default:
            return NULL;
    }
}

SensorHistory_t *hvac_get_metric_history(uint8_t metric_id)
{
    switch (metric_id) {
        case HVAC_METRIC_SUPPLY_TEMP:
            return &s_supply_temp_history;
        case HVAC_METRIC_RETURN_TEMP:
            return &s_return_temp_history;
        case HVAC_METRIC_INDOOR_HUMIDITY:
            return &s_indoor_humidity_history;
        case HVAC_METRIC_AMBIENT_TEMP:
            return &s_ambient_temp_history;
        default:
            return NULL;
    }
}
