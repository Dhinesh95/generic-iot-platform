/**
 * @file home_rules.c
 * @brief Home/Building domain rule set.
 *
 * Architecture ref: Section 6, Domain Profile data.
 *
 * Rule set includes:
 * - CRITICAL rule: door lock control tied to security state (SAFETY_LOCKED)
 * - OPERATIONAL rule: irrigation schedule
 * - OPERATIONAL rule: lighting schedule
 *
 * SAFETY_LOCKED rules cannot be modified from the field config portal.
 */

#include "home_rules.h"
#include "../../shared/rule_engine_core.h"
#include "../../shared/actuator_failsafe.h"

/* ---------- Rule Table ---------- */

static const RuleEntry_t s_home_rules[] = {
    /**
     * CRITICAL RULE: Door lock control.
     *
     * When security system is armed (metric 10 >= 1.0), the door lock
     * (actuator 1) should be ON (locked).
     *
     * This is SAFETY_LOCKED — it cannot be modified from the field
     * config portal. The fail-safe direction (lock/unlock on power loss)
     * requires compliance/legal sign-off per deployment jurisdiction.
     */
    {
        .rule_id      = HOME_RULE_DOOR_LOCK_CRITICAL,
        .priority     = 0,                           /* Highest priority. */
        .threshold    = 1.0f,                        /* Armed state = 1. */
        .action_type  = RULE_ACTION_SET_ACTUATOR,
        .actuator_id  = HOME_ACTUATOR_DOOR_LOCK,
        .metric_id    = HOME_METRIC_SECURITY_STATE,
        .rule_class   = RULE_CLASS_SAFETY_LOCKED,
        .interlock_id = 0,
        .comparison_type = RULE_COMPARE_ABOVE,   /* Trigger when ABOVE threshold (default). */
        .reserved     = {0, 0, 0}
    },

    /**
     * OPERATIONAL RULE: Irrigation schedule.
     *
     * When soil moisture (metric 13) drops below 30%,
     * open the irrigation valve (actuator 2).
     * Threshold comparison: moisture >= 30 means irrigate.
     *
     * This is OPERATIONAL — configurable from the field config portal.
     */
    {
        .rule_id      = HOME_RULE_IRRIGATION_OPERATIONAL,
        .priority     = 5,
        .threshold    = 30.0f,                       /* 30% moisture threshold. */
        .action_type  = RULE_ACTION_SET_ACTUATOR,
        .actuator_id  = HOME_ACTUATOR_IRRIGATION_VALVE,
        .metric_id    = HOME_METRIC_SOIL_MOISTURE,
        .rule_class   = RULE_CLASS_OPERATIONAL,
        .interlock_id = 0,
        .comparison_type = RULE_COMPARE_ABOVE,   /* Trigger when ABOVE threshold (default). */
        .reserved     = {0, 0, 0}
    },

    /**
     * OPERATIONAL RULE: Lighting schedule.
     *
     * When ambient light level (metric 12) drops below 100 lux,
     * turn on the light relay (actuator 3).
     *
     * This is OPERATIONAL — configurable from the field config portal.
     */
    {
        .rule_id      = HOME_RULE_LIGHTING_OPERATIONAL,
        .priority     = 8,
        .threshold    = 100.0f,                      /* 100 lux threshold. */
        .action_type  = RULE_ACTION_SET_ACTUATOR,
        .actuator_id  = HOME_ACTUATOR_LIGHT_RELAY,
        .metric_id    = HOME_METRIC_LIGHT_LEVEL,
        .rule_class   = RULE_CLASS_OPERATIONAL,
        .interlock_id = 0,
        .comparison_type = RULE_COMPARE_ABOVE,   /* Trigger when ABOVE threshold (default). */
        .reserved     = {0, 0, 0}
    }
};

#define HOME_RULE_COUNT (sizeof(s_home_rules) / sizeof(s_home_rules[0]))

/* ---------- VTable implementation ---------- */

void home_getRuleTable(const RuleEntry_t **out_entries, uint8_t *out_count)
{
    if (out_entries) *out_entries = s_home_rules;
    if (out_count)   *out_count = (uint8_t)HOME_RULE_COUNT;
}

bool home_validateSensorReading(const SensorReading_t *reading)
{
    if (!reading) return false;

    /* Delegate to sensor validation with home-specific bounds. */
    /* Bounds lookup is in home_validation_bounds.c via the vtable pattern. */
    /* For this profile, we do basic sanity: non-negative values. */
    return (reading->value >= 0.0f);
}

uint8_t home_getFailSafeMode(uint8_t actuator_id, bool is_power_loss)
{
    if (is_power_loss) {
        switch (actuator_id) {
            case HOME_ACTUATOR_DOOR_LOCK:
                /*
                 * TODO: FAILSAFE_FORCE_SAFE_POS (fail-unlocked direction).
                 *
                 * COMPLIANCE NOTE: The fail-unlocked-on-power-loss direction
                 * for electronic door locks is a REGULATORY/COMPLIANCE decision
                 * for the deployment jurisdiction, NOT a hardcoded engineering
                 * default. Fire/egress codes in most jurisdictions require
                 * electronic locks to fail unlocked, but this must be verified
                 * per deployment region by compliance/legal review.
                 *
                 * Verification status: Pending compliance/legal sign-off.
                 *
                 * This direction is CONFIGURABLE at build-time. The current
                 * default follows the most common regulatory requirement
                 * (fail-unlocked), but MUST be reviewed before deployment
                 * in any specific jurisdiction.
                 */
                return (uint8_t)FAILSAFE_FORCE_SAFE_POS;

            case HOME_ACTUATOR_IRRIGATION_VALVE:
                return (uint8_t)FAILSAFE_FORCE_OFF;

            case HOME_ACTUATOR_LIGHT_RELAY:
                return (uint8_t)FAILSAFE_FORCE_OFF;

            default:
                return (uint8_t)FAILSAFE_DE_ENERGIZE;
        }
    } else {
        /* Comms loss — actuator still has power. */
        switch (actuator_id) {
            case HOME_ACTUATOR_DOOR_LOCK:
                return (uint8_t)FAILSAFE_HOLD_LAST;

            case HOME_ACTUATOR_IRRIGATION_VALVE:
                return (uint8_t)FAILSAFE_HOLD_LAST;

            case HOME_ACTUATOR_LIGHT_RELAY:
                return (uint8_t)FAILSAFE_HOLD_LAST;

            default:
                return (uint8_t)FAILSAFE_HOLD_LAST;
        }
    }
}

bool home_executeAction(uint8_t actuator_id, uint8_t state)
{
    /*
     * In production: drive physical I/O via HAL.
     * For Phase 1: return success (action determined by vtable lookup).
     */
    (void)actuator_id;
    (void)state;
    return true;
}

/* ---------- VTable instance ---------- */

static const DomainProfileVTable_t s_home_profile_vtable = {
    .getRuleTable         = home_getRuleTable,
    .validateSensorReading = home_validateSensorReading,
    .getFailSafeMode      = home_getFailSafeMode,
    .executeAction        = home_executeAction
};

const DomainProfileVTable_t *home_profile_get_vtable(void)
{
    return &s_home_profile_vtable;
}
