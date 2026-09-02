/**
 * @file hvac_rules.c
 * @brief HVAC domain rule set implementation.
 *
 * Architecture ref: Section 6, Domain Profile data.
 *
 * Rules:
 * 1. CRITICAL: Compressor post-run lockout — shut down compressor
 *    when supply air temp indicates residual heat from a recent run
 *    (supply temp > 45°C). This is a temperature-threshold protection,
 *    NOT time-based short-cycle protection.
 * 2. CRITICAL: Over-temperature protection — shut down supply if
 *    supply air temperature exceeds 55°C (fire/equipment damage risk).
 * 3. OPERATIONAL: Comfort setpoint scheduling — maintain return air
 *    temperature between 20-24°C (heating) or 22-26°C (cooling).
 *
 * Real short-cycle protection (minimum off-time before restart) is
 * implemented as a separate stateful mechanism via hvac_short_cycle_*()
 * functions, not as a RuleEntry_t — because it requires timestamp
 * tracking that cannot fit in the 16-byte rule struct.
 */

#include "hvac_rules.h"
#include "hvac_validation_bounds.h"
#include "hvac_failsafe.h"
#include "../../shared/rule_engine_core.h"
#include "../../shared/sensor_validation.h"
#include "../../shared/actuator_failsafe.h"
#include <string.h>

/* ---------- Rule table ---------- */

static RuleEntry_t s_hvac_rules[] = {
    /**
     * CRITICAL: Compressor post-run lockout.
     *
     * If the supply air temperature is above 45°C, shut down the
     * compressor. This catches residual heat from a recent run cycle
     * and prevents immediate restart while ductwork is still warm.
     *
     * NOTE: This is a temperature-threshold protection, NOT true
     * short-cycle protection. True short-cycle protection (minimum
     * off-time enforcement) is handled separately by
     * hvac_short_cycle_check() / hvac_short_cycle_record_off().
     *
     * SAFETY_LOCKED: this protects equipment from damage and must
     * not be modified via the field config portal.
     */
    {
        .rule_id          = HVAC_RULE_COMPRESSOR_POST_RUN_LOCKOUT,
        .priority         = 0,                           /* Highest priority. */
        .threshold        = 45.0f,
        .action_type      = RULE_ACTION_SET_ACTUATOR,
        .actuator_id      = HVAC_ACTUATOR_COMPRESSOR_RELAY,
        .metric_id        = HVAC_METRIC_SUPPLY_TEMP,
        .rule_class       = RULE_CLASS_SAFETY_LOCKED,
        .interlock_id     = 0,
        .comparison_type  = RULE_COMPARE_ABOVE,
        .reserved         = {0, 0, 0}
    },

    /**
     * CRITICAL: Over-temperature protection.
     *
     * If supply air temperature exceeds 55°C, shut down the
     * compressor immediately. This temperature indicates a
     * refrigerant leak, stuck expansion valve, or fire risk.
     *
     * SAFETY_LOCKED: equipment and fire safety.
     */
    {
        .rule_id          = HVAC_RULE_OVERTEMP_PROTECT,
        .priority         = 0,                           /* Highest priority. */
        .threshold        = 55.0f,
        .action_type      = RULE_ACTION_SET_ACTUATOR,
        .actuator_id      = HVAC_ACTUATOR_COMPRESSOR_RELAY,
        .metric_id        = HVAC_METRIC_SUPPLY_TEMP,
        .rule_class       = RULE_CLASS_SAFETY_LOCKED,
        .interlock_id     = 0,
        .comparison_type  = RULE_COMPARE_ABOVE,
        .reserved         = {0, 0, 0}
    },

    /**
     * OPERATIONAL: Comfort setpoint — heating mode.
     *
     * If return air temperature drops below 20°C, activate the
     * fan to circulate warm air.
     *
     * OPERATIONAL: comfort rule, can be adjusted via config portal.
     */
    {
        .rule_id          = HVAC_RULE_COMFORT_SETPOINT,
        .priority         = 5,
        .threshold        = 20.0f,
        .action_type      = RULE_ACTION_SET_ACTUATOR,
        .actuator_id      = HVAC_ACTUATOR_FAN_MOTOR,
        .metric_id        = HVAC_METRIC_RETURN_TEMP,
        .rule_class       = RULE_CLASS_OPERATIONAL,
        .interlock_id     = 0,
        .comparison_type  = RULE_COMPARE_BELOW,
        .reserved         = {0, 0, 0}
    }
};

#define HVAC_RULE_COUNT (sizeof(s_hvac_rules) / sizeof(s_hvac_rules[0]))

/* ---------- Vtable functions ---------- */

void hvac_getRuleTable(const RuleEntry_t **out_entries, uint8_t *out_count)
{
    if (out_entries) *out_entries = s_hvac_rules;
    if (out_count)   *out_count   = HVAC_RULE_COUNT;
}

bool hvac_validateSensorReading(const SensorReading_t *reading)
{
    if (!reading) return false;

    /* Look up validation bounds for this metric. */
    const SensorValidationBounds_t *bounds =
        hvac_get_validation_bounds(reading->metric_id);
    if (!bounds) return false;  /* Unknown metric — reject. */

    /* Check physical range. */
    if (reading->value < bounds->min_physical ||
        reading->value > bounds->max_physical) {
        return false;
    }

    return true;
}

uint8_t hvac_getFailSafeMode(uint8_t actuator_id, bool is_power_loss)
{
    /* Look up the fail-safe table entry. */
    if (is_power_loss) {
        /* Return the power_loss_mode for this actuator. */
        switch (actuator_id) {
            case HVAC_ACTUATOR_COMPRESSOR_RELAY: return FAILSAFE_FORCE_OFF;
            case HVAC_ACTUATOR_DAMPER_MOTOR:     return FAILSAFE_FORCE_SAFE_POS;
            case HVAC_ACTUATOR_FAN_MOTOR:        return FAILSAFE_FORCE_OFF;
            default: return FAILSAFE_DE_ENERGIZE;
        }
    } else {
        /* Comms loss returns the mode (timeout triggers later). */
        switch (actuator_id) {
            case HVAC_ACTUATOR_COMPRESSOR_RELAY: return FAILSAFE_FORCE_OFF;
            case HVAC_ACTUATOR_DAMPER_MOTOR:     return FAILSAFE_HOLD_LAST;
            case HVAC_ACTUATOR_FAN_MOTOR:        return FAILSAFE_HOLD_LAST;
            default: return FAILSAFE_HOLD_LAST;
        }
    }
}

bool hvac_executeAction(uint8_t actuator_id, uint8_t state)
{
    /* In production: drive the actuator hardware.
     * For testing: accept any valid command. */
    (void)actuator_id;
    (void)state;
    return true;
}

/* ---------- Short-cycle protection (stateful, time-based) ---------- */

/**
 * Short-cycle protection tracks when the compressor last turned off
 * and enforces a minimum off-time (HVAC_MIN_COMPRESSOR_OFF_SEC)
 * before allowing restart.
 *
 * This CANNOT be expressed as a RuleEntry_t because:
 * - It requires stateful timestamp tracking (last_compressor_off_time)
 * - It compares elapsed time since last-off, not a sensor value
 *   against a threshold
 * - The RuleEntry_t struct is 16 bytes packed with no room for
 *   stateful timing fields
 *
 * Architecture ref: Section 6 (rule engine handles threshold-based rules;
 * time-based interlocks are a separate mechanism).
 */
static struct {
    uint32_t last_off_time_sec;   /**< Timestamp when compressor last turned off. 0 = never tracked. */
    bool     tracked;             /**< true if last_off_time_sec is valid. */
    bool     clock_unreliable;    /**< true if clock went backward — use fallback delay. */
} s_short_cycle_state = {0, false, false};

void hvac_short_cycle_record_off(uint32_t current_time_sec)
{
    s_short_cycle_state.last_off_time_sec = current_time_sec;
    s_short_cycle_state.tracked = true;
}

bool hvac_short_cycle_check(uint32_t current_time_sec)
{
    if (!s_short_cycle_state.tracked) {
        /* Compressor has never been tracked — allow restart. */
        return true;
    }

    if (current_time_sec < s_short_cycle_state.last_off_time_sec) {
        /*
         * Clock went backward — FAIL SAFE (block restart).
         *
         * Design choice: equipment-protection mechanisms default to the
         * safe/conservative behavior under uncertainty, not the
         * available/permissive one. This is the opposite trade-off from a
         * life-safety egress lock (e.g. the Home door lock, SR-015), where
         * availability (egress) is prioritized over restriction.
         *
         * Rationale: the most likely cause of clock unreliability is a
         * power brownout or reboot — also the scenario where an immediate
         * compressor restart is most likely to be attempted and most needs
         * to be blocked. Under clock uncertainty, we lack sufficient
         * information to confirm the minimum off-time has elapsed, so we
         * apply a bounded conservative delay (HVAC_CLOCK_UNRELIABLE_FALLBACK_SEC)
         * rather than either permitting immediately (risking compressor
         * damage from liquid slugging due to unequalized refrigerant
         * pressures) or blocking forever (risking indefinite lockout
         * if the clock never recovers).
         *
         * The clock_unreliable flag causes subsequent calls (once the clock
         * resumes moving forward) to use the shorter fallback delay instead
         * of the normal minimum off-time — the system cannot know how long
         * it was actually off during the unreliable period, so it applies
         * the conservative fallback rather than assuming the full normal
         * off-time has elapsed.
         */
        s_short_cycle_state.clock_unreliable = true;
        return false;
    }

    uint32_t elapsed = current_time_sec - s_short_cycle_state.last_off_time_sec;

    /*
     * If the clock was previously unreliable, use the shorter fallback
     * delay. Once the clock is moving forward again and enough time has
     * elapsed, allow restart — but we cannot assume the full normal
     * off-time was respected during the unreliable period.
     */
    if (s_short_cycle_state.clock_unreliable) {
        return (elapsed >= HVAC_CLOCK_UNRELIABLE_FALLBACK_SEC);
    }

    return (elapsed >= HVAC_MIN_COMPRESSOR_OFF_SEC);
}

void hvac_short_cycle_reset(void)
{
    s_short_cycle_state.last_off_time_sec = 0;
    s_short_cycle_state.tracked = false;
    s_short_cycle_state.clock_unreliable = false;
}

uint32_t hvac_short_cycle_get_min_off_sec(void)
{
    return HVAC_MIN_COMPRESSOR_OFF_SEC;
}

uint32_t hvac_short_cycle_get_last_off(void)
{
    return s_short_cycle_state.last_off_time_sec;
}

uint32_t hvac_clock_unreliable_fallback_sec(void)
{
    return HVAC_CLOCK_UNRELIABLE_FALLBACK_SEC;
}

/* ---------- Vtable instance ---------- */

static const DomainProfileVTable_t s_hvac_profile_vtable = {
    .getRuleTable          = hvac_getRuleTable,
    .validateSensorReading = hvac_validateSensorReading,
    .getFailSafeMode       = hvac_getFailSafeMode,
    .executeAction         = hvac_executeAction
};

const DomainProfileVTable_t *hvac_profile_get_vtable(void)
{
    return &s_hvac_profile_vtable;
}
