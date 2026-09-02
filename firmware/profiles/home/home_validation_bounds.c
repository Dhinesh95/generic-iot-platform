/**
 * @file home_validation_bounds.c
 * @brief Home/Building sensor plausibility bounds.
 *
 * Architecture ref: Section 9, Bounds (Domain Profile, Data Not Code).
 *
 * These are domain-specific data — hardcoded here for Phase 1 but
 * intended to be tuned from real field data per the architecture
 * document's guidance.
 *
 * Bounds set too tight will false-positive on legitimate but unusual
 * readings. Bounds must come from domain-expert review and real field
 * data, not estimation.
 */

#include "home_validation_bounds.h"
#include <string.h>

/* ---------- Validation bounds for Home/Building metrics ---------- */

/**
 * Door contact sensor (binary: 0=closed, 1=open).
 *
 * Physical range: 0.0 to 1.0 (binary).
 * Rate of change: max 2.0/sec (door cannot change faster than this).
 * Stuck timeout: 0 (binary sensor, stable values are expected).
 * Cross-check: none (single sensor per door).
 */
static const SensorValidationBounds_t s_door_contact_bounds = {
    .min_physical        = 0.0f,
    .max_physical        = 1.0f,
    .max_rate_of_change  = 2.0f,
    .stuck_timeout_sec   = 0,     /* Binary sensor — stable values are normal. */
    .cross_check_node_id = 0      /* No redundant sensor. */
};

/**
 * Ambient light level sensor (analog: 0-1000 lux).
 *
 * Physical range: 0.0 to 1000.0 lux.
 * Rate of change: max 500.0/sec (sunrise/sunset transitions are slow).
 * Stuck timeout: 3600 sec (1 hour — a light sensor stuck for an hour
 *   is suspect; natural light always varies).
 * Cross-check: none.
 */
static const SensorValidationBounds_t s_light_level_bounds = {
    .min_physical        = 0.0f,
    .max_physical        = 1000.0f,
    .max_rate_of_change  = 500.0f,
    .stuck_timeout_sec   = 3600,  /* 1 hour — suspect if unchanged. */
    .cross_check_node_id = 0
};

/**
 * Soil moisture sensor (analog: 0-100%).
 *
 * Physical range: 0.0 to 100.0 percent.
 * Rate of change: max 10.0/sec (moisture changes slowly).
 * Stuck timeout: 7200 sec (2 hours — soil moisture changes slowly
 *   but should drift; stuck for 2 hours is suspect).
 * Cross-check: none.
 */
static const SensorValidationBounds_t s_soil_moisture_bounds = {
    .min_physical        = 0.0f,
    .max_physical        = 100.0f,
    .max_rate_of_change  = 10.0f,
    .stuck_timeout_sec   = 7200,  /* 2 hours. */
    .cross_check_node_id = 0
};

/* ---------- History buffers ---------- */

static SensorHistory_t s_door_contact_history;
static SensorHistory_t s_light_level_history;
static SensorHistory_t s_soil_moisture_history;

/* ---------- Public API ---------- */

const SensorValidationBounds_t *home_get_validation_bounds(uint8_t metric_id)
{
    switch (metric_id) {
        case HOME_METRIC_DOOR_CONTACT:
            return &s_door_contact_bounds;
        case HOME_METRIC_LIGHT_LEVEL:
            return &s_light_level_bounds;
        case HOME_METRIC_SOIL_MOISTURE:
            return &s_soil_moisture_bounds;
        default:
            return NULL;
    }
}

SensorHistory_t *home_get_metric_history(uint8_t metric_id)
{
    switch (metric_id) {
        case HOME_METRIC_DOOR_CONTACT:
            return &s_door_contact_history;
        case HOME_METRIC_LIGHT_LEVEL:
            return &s_light_level_history;
        case HOME_METRIC_SOIL_MOISTURE:
            return &s_soil_moisture_history;
        default:
            return NULL;
    }
}
