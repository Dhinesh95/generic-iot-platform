/**
 * @file agriculture_rules.c
 * @brief Agriculture domain rule set.
 *
 * Architecture ref: Section 6, Domain Profile data.
 *
 * Rule set includes:
 * - CRITICAL rule: irrigation pump dry-run protection (SAFETY_LOCKED)
 * - OPERATIONAL rule: soil-moisture-triggered irrigation scheduling
 * - OPERATIONAL rule: frost protection
 *
 * SAFETY_LOCKED rules cannot be modified from the field config portal.
 */

#include "agriculture_rules.h"
#include "../../shared/rule_engine_core.h"
#include "../../shared/actuator_failsafe.h"

/* ---------- Rule Table ---------- */

static const RuleEntry_t s_agriculture_rules[] = {
    /**
     * CRITICAL RULE: Irrigation pump dry-run protection.
     *
     * When water level (metric 23) drops below 10%, stop the pump
     * (actuator 1) to prevent dry-run damage.
     *
     * This is SAFETY_LOCKED — it cannot be modified from the field
     * config portal. Running a pump dry causes bearing damage and
     * is a safety concern (overheating, potential fire risk in
     * some pump types).
     */
    {
        .rule_id      = AGRI_RULE_PUMP_DRY_RUN_CRITICAL,
        .priority     = 0,                           /* Highest priority. */
        .threshold    = 10.0f,                       /* Water level < 10% = stop pump. */
        .action_type  = RULE_ACTION_SET_ACTUATOR,
        .actuator_id  = AGRI_ACTUATOR_IRRIGATION_PUMP,
        .metric_id    = AGRI_METRIC_WATER_LEVEL,
        .rule_class   = RULE_CLASS_SAFETY_LOCKED,
        .interlock_id = 0,
        .comparison_type = RULE_COMPARE_BELOW,       /* Trigger when BELOW threshold. */
        .reserved     = {0, 0, 0}
    },

    /**
     * OPERATIONAL RULE: Soil-moisture-triggered irrigation.
     *
     * When soil moisture (metric 20) drops below 25%, open the
     * irrigation valve (actuator 2) and start the pump.
     *
     * This is OPERATIONAL — configurable from the field config portal.
     */
    {
        .rule_id      = AGRI_RULE_IRRIGATION_OPERATIONAL,
        .priority     = 5,
        .threshold    = 25.0f,                       /* 25% moisture threshold. */
        .action_type  = RULE_ACTION_SET_ACTUATOR,
        .actuator_id  = AGRI_ACTUATOR_IRRIGATION_VALVE,
        .metric_id    = AGRI_METRIC_SOIL_MOISTURE,
        .rule_class   = RULE_CLASS_OPERATIONAL,
        .interlock_id = 0,
        .comparison_type = RULE_COMPARE_BELOW,       /* Trigger when BELOW threshold. */
        .reserved     = {0, 0, 0}
    },

    /**
     * OPERATIONAL RULE: Frost protection.
     *
     * When ambient temperature (metric 21) drops below 2°C,
     * open the frost protection valve (actuator 3) to circulate
     * warm water (if available from a heat source).
     *
     * This is OPERATIONAL — configurable from the field config portal.
     */
    {
        .rule_id      = AGRI_RULE_FROST_PROTECT_OPERATIONAL,
        .priority     = 3,
        .threshold    = 2.0f,                        /* 2°C frost threshold. */
        .action_type  = RULE_ACTION_SET_ACTUATOR,
        .actuator_id  = AGRI_ACTUATOR_FROST_VALVE,
        .metric_id    = AGRI_METRIC_AMBIENT_TEMP,
        .rule_class   = RULE_CLASS_OPERATIONAL,
        .interlock_id = 0,
        .comparison_type = RULE_COMPARE_BELOW,       /* Trigger when BELOW threshold (frost). */
        .reserved     = {0, 0, 0}
    }
};

#define AGRI_RULE_COUNT (sizeof(s_agriculture_rules) / sizeof(s_agriculture_rules[0]))

/* ---------- VTable implementation ---------- */

void agriculture_getRuleTable(const RuleEntry_t **out_entries, uint8_t *out_count)
{
    if (out_entries) *out_entries = s_agriculture_rules;
    if (out_count)   *out_count = (uint8_t)AGRI_RULE_COUNT;
}

bool agriculture_validateSensorReading(const SensorReading_t *reading)
{
    if (!reading) return false;

    /* Agriculture-specific validation: non-negative values for all metrics. */
    return (reading->value >= 0.0f);
}

uint8_t agriculture_getFailSafeMode(uint8_t actuator_id, bool is_power_loss)
{
    if (is_power_loss) {
        switch (actuator_id) {
            case AGRI_ACTUATOR_IRRIGATION_PUMP:
                return (uint8_t)FAILSAFE_FORCE_OFF;

            case AGRI_ACTUATOR_IRRIGATION_VALVE:
                /*
                 * FAILSAFE_DE_ENERGIZE for a normally-closed valve:
                 * on power loss, the valve springs shut, stopping water
                 * flow. This is the conservative default — it wastes no
                 * water but may leave crops un-irrigated during an outage.
                 * Alternative (fail-open) would waste water but protect
                 * crops. Decision depends on crop value vs water cost.
                 */
                return (uint8_t)FAILSAFE_DE_ENERGIZE;

            case AGRI_ACTUATOR_FROST_VALVE:
                return (uint8_t)FAILSAFE_DE_ENERGIZE;

            default:
                return (uint8_t)FAILSAFE_DE_ENERGIZE;
        }
    } else {
        /* Comms loss — actuator still has power. */
        switch (actuator_id) {
            case AGRI_ACTUATOR_IRRIGATION_PUMP:
                return (uint8_t)FAILSAFE_FORCE_OFF;

            case AGRI_ACTUATOR_IRRIGATION_VALVE:
                return (uint8_t)FAILSAFE_HOLD_LAST;

            case AGRI_ACTUATOR_FROST_VALVE:
                return (uint8_t)FAILSAFE_HOLD_LAST;

            default:
                return (uint8_t)FAILSAFE_HOLD_LAST;
        }
    }
}

bool agriculture_executeAction(uint8_t actuator_id, uint8_t state)
{
    /*
     * In production: drive physical I/O via HAL.
     * For Phase 2: return success (action determined by vtable lookup).
     */
    (void)actuator_id;
    (void)state;
    return true;
}

/* ---------- VTable instance ---------- */

static const DomainProfileVTable_t s_agriculture_profile_vtable = {
    .getRuleTable          = agriculture_getRuleTable,
    .validateSensorReading = agriculture_validateSensorReading,
    .getFailSafeMode       = agriculture_getFailSafeMode,
    .executeAction         = agriculture_executeAction
};

const DomainProfileVTable_t *agriculture_profile_get_vtable(void)
{
    return &s_agriculture_profile_vtable;
}
