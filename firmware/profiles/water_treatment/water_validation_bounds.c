/**
 * @file water_validation_bounds.c
 * @brief Water Treatment sensor plausibility bounds.
 *
 * Architecture ref: Section 9, Bounds (Domain Profile, Data Not Code).
 *
 * These are domain-specific data — hardcoded here but intended to be
 * tuned from real field data.
 *
 * CRITICAL DESIGN PRINCIPLE (from architecture Section 9):
 * "A stuck 'chlorine level fine' reading must not be allowed to let
 * a dosing pump continue indefinitely." Every validation failure must
 * route to the domain's fail-safe mode, never continue operating on
 * a last-known-good value.
 *
 * Bounds verification: each bound is designed to catch a specific
 * failure mode. If the bound doesn't actually protect against the
 * failure it claims to, it's worse than useless (provides false
 * assurance). The comments below explain what each bound catches.
 */

#include "water_validation_bounds.h"
#include <string.h>

/* ---------- Validation bounds for Water Treatment metrics ---------- */

/**
 * Chlorine level sensor (analog: 0-10 ppm).
 *
 * Physical range: 0.0 to 10.0 ppm (typical free chlorine sensors
 *   read 0-10 ppm; values above 10 ppm are sensor saturation or
 *   hardware failure, not real chlorine levels).
 *
 * Rate of change: max 2.0 ppm/sec.
 *   Chlorine dosing changes slowly — a jump of >2 ppm in one second
 *   is almost certainly sensor noise or electrical interference, not
 *   real chemical change. Even rapid dosing changes take tens of
 *   seconds to propagate through the system.
 *
 * Stuck timeout: 1800 sec (30 minutes).
 *   ARCHITECTURE SECTION 9: "A chlorine reading unchanged for an
 *   hour is suspect (dosing/consumption should cause drift)." We use
 *   30 minutes — aggressive enough to catch a stuck sensor before
 *   excessive over/under-dose accumulates, but not so aggressive
 *   that slow-drift sensors trigger false positives. A chlorine
 *   sensor stuck at a "safe" value while dosing continues is the
 *   worst-case failure mode — it prevents the safety rules from
 *   ever triggering.
 *
 * Physical failure mode caught: stuck sensor at "safe" value while
 *   dosing is actually over/under — the most dangerous sensor failure
 *   in a water treatment system.
 */
static const SensorValidationBounds_t s_chlorine_bounds = {
    .min_physical        = 0.0f,
    .max_physical        = 10.0f,
    .max_rate_of_change  = 2.0f,
    .stuck_timeout_sec   = 1800,  /* 30 minutes. */
    .cross_check_node_id = 0
};

/**
 * pH sensor (analog: 0-14).
 *
 * Physical range: 0.0 to 14.0 (full pH scale).
 *
 * Rate of change: max 1.0 pH/sec.
 *   pH changes slowly in treated water systems — a jump of >1 pH
 *   unit in one second is sensor noise, not real chemical change.
 *   Even rapid chemical additions take time to mix and register.
 *
 * Stuck timeout: 3600 sec (1 hour).
 *   pH is more stable than chlorine in treated water — a reading
 *   unchanged for an hour is suspect but less urgently so than
 *   chlorine. pH drift is driven by buffering capacity of the water,
 *   which changes slowly. A stuck pH sensor could mask corrosion
 *   conditions (pH drifting acidic) or scale formation conditions
 *   (pH drifting alkaline).
 *
 * Physical failure mode caught: stuck sensor masking gradual pH drift
 *   that could damage distribution infrastructure or indicate
 *   treatment process failure.
 */
static const SensorValidationBounds_t s_ph_bounds = {
    .min_physical        = 0.0f,
    .max_physical        = 14.0f,
    .max_rate_of_change  = 1.0f,
    .stuck_timeout_sec   = 3600,  /* 1 hour. */
    .cross_check_node_id = 0
};

/**
 * Tank level sensor (analog: 0-100%).
 *
 * Physical range: 0.0 to 100.0 percent.
 *
 * Rate of change: max 5.0%/sec.
 *   Tank level changes with demand (outflow) and refill (inflow).
 *   A jump of >5% in one second is almost certainly sensor noise —
 *   even large-demand events take seconds to drain a tank.
 *
 * Stuck timeout: 1800 sec (30 minutes).
 *   Tank level should change with demand patterns. Stuck for 30
 *   minutes during active demand suggests a failed sensor. A stuck
 *   "full" reading could mask a supply outage; a stuck "empty"
 *   reading could cause unnecessary refill cycles.
 *
 * Physical failure mode caught: stuck "full" sensor masking supply
 *   outage (users run out of treated water with no alarm).
 */
static const SensorValidationBounds_t s_tank_level_bounds = {
    .min_physical        = 0.0f,
    .max_physical        = 100.0f,
    .max_rate_of_change  = 5.0f,
    .stuck_timeout_sec   = 1800,  /* 30 minutes. */
    .cross_check_node_id = 0
};

/**
 * Flow rate sensor (analog: 0-50 L/min).
 *
 * Physical range: 0.0 to 50.0 L/min.
 *
 * Rate of change: max 10.0 L/min/sec.
 *   Flow rate can change rapidly when valves open/close, but a
 *   change of >10 L/min in one second indicates sensor noise or
 *   water hammer, not real flow change.
 *
 * Stuck timeout: 900 sec (15 minutes).
 *   Flow rate should vary with demand. Stuck for 15 minutes during
 *   active operation suggests a failed sensor. A stuck "zero flow"
 *   reading could mask a pump failure; a stuck "normal flow" could
 *   mask a pipe break.
 *
 * Physical failure mode caught: stuck "normal flow" sensor masking
 *   pipe break or pump failure (water loss without detection).
 */
static const SensorValidationBounds_t s_flow_rate_bounds = {
    .min_physical        = 0.0f,
    .max_physical        = 50.0f,
    .max_rate_of_change  = 10.0f,
    .stuck_timeout_sec   = 900,   /* 15 minutes. */
    .cross_check_node_id = 0
};

/* ---------- History buffers ---------- */

static SensorHistory_t s_chlorine_history;
static SensorHistory_t s_ph_history;
static SensorHistory_t s_tank_level_history;
static SensorHistory_t s_flow_rate_history;

/* ---------- Public API ---------- */

const SensorValidationBounds_t *water_get_validation_bounds(uint8_t metric_id)
{
    switch (metric_id) {
        case WATER_METRIC_CHLORINE_LEVEL:
            return &s_chlorine_bounds;
        case WATER_METRIC_PH:
            return &s_ph_bounds;
        case WATER_METRIC_TANK_LEVEL:
            return &s_tank_level_bounds;
        case WATER_METRIC_FLOW_RATE:
            return &s_flow_rate_bounds;
        default:
            return NULL;
    }
}

SensorHistory_t *water_get_metric_history(uint8_t metric_id)
{
    switch (metric_id) {
        case WATER_METRIC_CHLORINE_LEVEL:
            return &s_chlorine_history;
        case WATER_METRIC_PH:
            return &s_ph_history;
        case WATER_METRIC_TANK_LEVEL:
            return &s_tank_level_history;
        case WATER_METRIC_FLOW_RATE:
            return &s_flow_rate_history;
        default:
            return NULL;
    }
}
