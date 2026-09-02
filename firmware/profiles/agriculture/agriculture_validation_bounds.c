/**
 * @file agriculture_validation_bounds.c
 * @brief Agriculture sensor plausibility bounds.
 *
 * Architecture ref: Section 9, Bounds (Domain Profile, Data Not Code).
 *
 * These are domain-specific data — hardcoded here for Phase 2 but
 * intended to be tuned from real field data.
 *
 * Agriculture bounds are wider than Home's indoor bounds because
 * outdoor environments have更大的温度范围和湿度变化.
 */

#include "agriculture_validation_bounds.h"
#include <string.h>

/* ---------- Validation bounds for Agriculture metrics ---------- */

/**
 * Soil moisture sensor (analog: 0-100%).
 *
 * Physical range: 0.0 to 100.0 percent.
 * Rate of change: max 5.0/sec (soil moisture changes slowly).
 * Stuck timeout: 7200 sec (2 hours — soil moisture should drift
 *   with irrigation/rain/evaporation; stuck for 2 hours is suspect).
 */
static const SensorValidationBounds_t s_soil_moisture_bounds = {
    .min_physical        = 0.0f,
    .max_physical        = 100.0f,
    .max_rate_of_change  = 5.0f,
    .stuck_timeout_sec   = 7200,  /* 2 hours. */
    .cross_check_node_id = 0
};

/**
 * Ambient temperature sensor (analog: -40 to +60 C).
 *
 * Physical range: -40.0 to +60.0 °C (outdoor agricultural range).
 * Rate of change: max 5.0°C/sec (rapid changes are suspect —
 *   a sensor spike of 10°C in 1 second is almost certainly noise).
 * Stuck timeout: 3600 sec (1 hour — temperature always varies
 *   outdoors; stuck for an hour is suspect).
 */
static const SensorValidationBounds_t s_ambient_temp_bounds = {
    .min_physical        = -40.0f,
    .max_physical        = 60.0f,
    .max_rate_of_change  = 5.0f,
    .stuck_timeout_sec   = 3600,  /* 1 hour. */
    .cross_check_node_id = 0
};

/**
 * Ambient humidity sensor (analog: 0-100%).
 *
 * Physical range: 0.0 to 100.0 percent.
 * Rate of change: max 10.0/sec (humidity changes slowly outdoors).
 * Stuck timeout: 3600 sec (1 hour).
 */
static const SensorValidationBounds_t s_ambient_humidity_bounds = {
    .min_physical        = 0.0f,
    .max_physical        = 100.0f,
    .max_rate_of_change  = 10.0f,
    .stuck_timeout_sec   = 3600,  /* 1 hour. */
    .cross_check_node_id = 0
};

/**
 * Water level sensor (analog: 0-100%).
 *
 * Physical range: 0.0 to 100.0 percent.
 * Rate of change: max 2.0/sec (reservoir level changes slowly).
 * Stuck timeout: 1800 sec (30 minutes — water level should change
 *   with irrigation demand; stuck for 30 min is suspect).
 */
static const SensorValidationBounds_t s_water_level_bounds = {
    .min_physical        = 0.0f,
    .max_physical        = 100.0f,
    .max_rate_of_change  = 2.0f,
    .stuck_timeout_sec   = 1800,  /* 30 minutes. */
    .cross_check_node_id = 0
};

/* ---------- History buffers ---------- */

static SensorHistory_t s_soil_moisture_history;
static SensorHistory_t s_ambient_temp_history;
static SensorHistory_t s_ambient_humidity_history;
static SensorHistory_t s_water_level_history;

/* ---------- Public API ---------- */

const SensorValidationBounds_t *agriculture_get_validation_bounds(uint8_t metric_id)
{
    switch (metric_id) {
        case AGRI_METRIC_SOIL_MOISTURE:
            return &s_soil_moisture_bounds;
        case AGRI_METRIC_AMBIENT_TEMP:
            return &s_ambient_temp_bounds;
        case AGRI_METRIC_AMBIENT_HUMIDITY:
            return &s_ambient_humidity_bounds;
        case AGRI_METRIC_WATER_LEVEL:
            return &s_water_level_bounds;
        default:
            return NULL;
    }
}

SensorHistory_t *agriculture_get_metric_history(uint8_t metric_id)
{
    switch (metric_id) {
        case AGRI_METRIC_SOIL_MOISTURE:
            return &s_soil_moisture_history;
        case AGRI_METRIC_AMBIENT_TEMP:
            return &s_ambient_temp_history;
        case AGRI_METRIC_AMBIENT_HUMIDITY:
            return &s_ambient_humidity_history;
        case AGRI_METRIC_WATER_LEVEL:
            return &s_water_level_history;
        default:
            return NULL;
    }
}
