/**
 * @file water_rules.c
 * @brief Water Treatment (Industrial) domain rule set implementation.
 *
 * Architecture ref: Section 6, Domain Profile data.
 *
 * Rules:
 * 1. CRITICAL: Chlorine dosing safety — stop dosing if chlorine level
 *    exceeds safe maximum (over-dose: chemical exposure risk) or drops
 *    below minimum (sensor failure or complete depletion).
 * 2. CRITICAL: pH high safety — alarm and stop dosing if pH exceeds
 *    safe upper bound (scale formation risk above 8.5).
 * 3. CRITICAL: pH low safety — alarm and stop dosing if pH drops
 *    below safe lower bound (corrosion risk below 6.5).
 * 3. OPERATIONAL: Tank level refill scheduling — open supply valve
 *    when treated-water tank drops below threshold.
 *
 * SAFETY_LOCKED rules cannot be modified from the field config portal.
 *
 * IMPORTANT: This is the highest safety-stakes domain built so far.
 * Chemical dosing carries direct risk of harm to end users — not just
 * equipment damage. Every rule and fail-safe decision must be treated
 * with the same rigor as the HVAC short-cycle logic correction.
 */

#include "water_rules.h"
#include "water_validation_bounds.h"
#include "water_failsafe.h"
#include "../../shared/rule_engine_core.h"
#include "../../shared/sensor_validation.h"
#include "../../shared/actuator_failsafe.h"
#include <string.h>

/* ---------- Rule Table ---------- */

static const RuleEntry_t s_water_rules[] = {
    /**
     * CRITICAL RULE: Chlorine dosing safety.
     *
     * If chlorine level exceeds WATER_CHLORINE_SAFE_MAX_PPM (4.0 ppm),
     * stop the dosing valve to prevent chemical over-dose.
     *
     * Physical harm prevented: chlorine over-dose causes chemical
     * exposure risk — skin/eye irritation, respiratory issues, and
     * at extreme levels (>10 ppm) acute poisoning. The EPA MRDL
     * for chlorine is 4.0 ppm; exceeding this indicates a dosing
     * malfunction (stuck-open valve, incorrect dose calculation).
     *
     * SAFETY_LOCKED: protects end-user health, must not be modified
     * from field config portal.
     */
    {
        .rule_id          = WATER_RULE_CHLORINE_SAFETY,
        .priority         = 0,                           /* Highest priority. */
        .threshold        = WATER_CHLORINE_SAFE_MAX_PPM, /* 4.0 ppm. */
        .action_type      = RULE_ACTION_SET_ACTUATOR,
        .actuator_id      = WATER_ACTUATOR_DOSING_VALVE,
        .metric_id        = WATER_METRIC_CHLORINE_LEVEL,
        .rule_class       = RULE_CLASS_SAFETY_LOCKED,
        .interlock_id     = 0,
        .comparison_type  = RULE_COMPARE_ABOVE,          /* Trigger when ABOVE max safe. */
        .reserved         = {0, 0, 0}
    },

    /**
     * CRITICAL RULE: pH high safety (scale formation risk).
     *
     * If pH exceeds WATER_PH_SAFE_MAX (8.5), alarm and stop dosing.
     *
     * Physical harm prevented: pH > 8.5 (alkaline) causes scale
     * formation that clogs pipes, reduces disinfection effectiveness,
     * and produces bitter taste — operational and health concern.
     * Exceeding 8.5 indicates over-alkalization (excess lime/soda ash
     * dosing) or buffer exhaustion in the distribution system.
     *
     * SAFETY_LOCKED: protects end-user health and distribution system
     * integrity.
     */
    {
        .rule_id          = WATER_RULE_PH_HIGH_SAFETY,
        .priority         = 0,                           /* Highest priority. */
        .threshold        = WATER_PH_SAFE_MAX,           /* 8.5 pH. */
        .action_type      = RULE_ACTION_SET_ACTUATOR,
        .actuator_id      = WATER_ACTUATOR_DOSING_VALVE,
        .metric_id        = WATER_METRIC_PH,
        .rule_class       = RULE_CLASS_SAFETY_LOCKED,
        .interlock_id     = 0,
        .comparison_type  = RULE_COMPARE_ABOVE,          /* Trigger when ABOVE max safe pH. */
        .reserved         = {0, 0, 0}
    },

    /**
     * CRITICAL RULE: pH low safety (corrosion risk).
     *
     * If pH drops below WATER_PH_SAFE_MIN (6.5), alarm and stop dosing.
     *
     * Physical harm prevented: pH < 6.5 (acidic) causes corrosion of
     * distribution pipes, leaching of heavy metals (lead, copper) into
     * drinking water — direct health risk to consumers. This is a
     * SEPARATE failure mode from high pH (scale formation) and requires
     * a distinct audit-log entry for root-cause analysis.
     *
     * NOTE: this rule was added in Phase 6.1 to fix a conflation bug —
     * previously, low-pH detection was conflated with sensor validation
     * failure (both triggered the same code path). A pH of 6.0 is a
     * physically valid reading that indicates a real chemical condition,
     * NOT a sensor malfunction. The two events must produce distinct
     * audit-log entries and route through the rule engine, not the
     * sensor validation gate.
     *
     * SAFETY_LOCKED: protects end-user health.
     */
    {
        .rule_id          = WATER_RULE_PH_LOW_SAFETY,
        .priority         = 0,                           /* Highest priority. */
        .threshold        = WATER_PH_SAFE_MIN,           /* 6.5 pH. */
        .action_type      = RULE_ACTION_SET_ACTUATOR,
        .actuator_id      = WATER_ACTUATOR_DOSING_VALVE,
        .metric_id        = WATER_METRIC_PH,
        .rule_class       = RULE_CLASS_SAFETY_LOCKED,
        .interlock_id     = 0,
        .comparison_type  = RULE_COMPARE_BELOW,          /* Trigger when BELOW min safe pH. */
        .reserved         = {0, 0, 0}
    },

    /**
     * OPERATIONAL RULE: Tank level refill scheduling.
     *
     * When treated-water tank level drops below 20%, open the main
     * supply valve to refill from the treatment source.
     *
     * This is OPERATIONAL — configurable from the field config portal.
     * The refill threshold can be adjusted based on tank capacity,
     * demand patterns, and source availability.
     */
    {
        .rule_id          = WATER_RULE_TANK_REFILL,
        .priority         = 5,
        .threshold        = 20.0f,                       /* 20% tank level. */
        .action_type      = RULE_ACTION_SET_ACTUATOR,
        .actuator_id      = WATER_ACTUATOR_MAIN_SUPPLY_VALVE,
        .metric_id        = WATER_METRIC_TANK_LEVEL,
        .rule_class       = RULE_CLASS_OPERATIONAL,
        .interlock_id     = 0,
        .comparison_type  = RULE_COMPARE_BELOW,          /* Trigger when BELOW threshold. */
        .reserved         = {0, 0, 0}
    }
};

#define WATER_RULE_COUNT (sizeof(s_water_rules) / sizeof(s_water_rules[0]))

/* ---------- VTable implementation ---------- */

void water_getRuleTable(const RuleEntry_t **out_entries, uint8_t *out_count)
{
    if (out_entries) *out_entries = s_water_rules;
    if (out_count)   *out_count   = (uint8_t)WATER_RULE_COUNT;
}

bool water_validateSensorReading(const SensorReading_t *reading)
{
    if (!reading) return false;

    /* Look up validation bounds for this metric. */
    const SensorValidationBounds_t *bounds =
        water_get_validation_bounds(reading->metric_id);
    if (!bounds) return false;  /* Unknown metric — reject. */

    /* Check physical range. */
    if (reading->value < bounds->min_physical ||
        reading->value > bounds->max_physical) {
        return false;
    }

    return true;
}

uint8_t water_getFailSafeMode(uint8_t actuator_id, bool is_power_loss)
{
    if (is_power_loss) {
        switch (actuator_id) {
            case WATER_ACTUATOR_DOSING_VALVE:
                return (uint8_t)FAILSAFE_FORCE_OFF;
            case WATER_ACTUATOR_MAIN_SUPPLY_VALVE:
                return (uint8_t)FAILSAFE_DE_ENERGIZE;
            case WATER_ACTUATOR_CIRCULATION_PUMP:
                return (uint8_t)FAILSAFE_FORCE_OFF;
            default:
                return (uint8_t)FAILSAFE_DE_ENERGIZE;
        }
    } else {
        /* Comms loss — actuator still has power. */
        switch (actuator_id) {
            case WATER_ACTUATOR_DOSING_VALVE:
                return (uint8_t)FAILSAFE_FORCE_OFF;
            case WATER_ACTUATOR_MAIN_SUPPLY_VALVE:
                return (uint8_t)FAILSAFE_HOLD_LAST;
            case WATER_ACTUATOR_CIRCULATION_PUMP:
                return (uint8_t)FAILSAFE_HOLD_LAST;
            default:
                return (uint8_t)FAILSAFE_HOLD_LAST;
        }
    }
}

bool water_executeAction(uint8_t actuator_id, uint8_t state)
{
    /*
     * In production: drive physical I/O via HAL.
     * For testing: accept any valid command.
     */
    (void)actuator_id;
    (void)state;
    return true;
}

/* ---------- VTable instance ---------- */

static const DomainProfileVTable_t s_water_profile_vtable = {
    .getRuleTable          = water_getRuleTable,
    .validateSensorReading = water_validateSensorReading,
    .getFailSafeMode       = water_getFailSafeMode,
    .executeAction         = water_executeAction
};

const DomainProfileVTable_t *water_profile_get_vtable(void)
{
    return &s_water_profile_vtable;
}
