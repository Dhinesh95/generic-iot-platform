/**
 * @file hvac_failsafe.c
 * @brief HVAC actuator fail-safe decision table.
 *
 * Architecture ref: Section 10, HVAC table.
 *
 * +-------------------+------------+------------------------+----------------------------+-----------+
 * | Actuator          | Criticality| Power-Loss Mode        | Comms-Loss Mode            | Timeout   |
 * +-------------------+------------+------------------------+----------------------------+-----------+
 * | Compressor relay  | CRITICAL   | FORCE_OFF              | FORCE_OFF (30s)            | 30s       |
 * | Damper motor      | STANDARD   | FORCE_SAFE_POS (50%)   | HOLD_LAST (300s) → SAFE_POS| 300s      |
 * | Fan motor         | STANDARD   | FORCE_OFF              | HOLD_LAST (300s) → OFF     | 300s      |
 * +-------------------+------------+------------------------+----------------------------+-----------+
 *
 * Reasoning:
 * - Compressor: FORCE_OFF on both power and comms loss. Uncontrolled
 *   cycling risks equipment damage. Brief comms blips should not
 *   restart the compressor without monitoring.
 * - Damper: FORCE_SAFE_POS (50% open) on power loss. Full-closed
 *   loses ventilation (stale air, CO2 buildup); full-open wastes
 *   energy (conditioned air escapes). Midpoint is the safe default —
 *   maintains baseline ventilation without excessive energy loss.
 *   On comms loss, HOLD_LAST for 300s then FORCE_SAFE_POS — brief
 *   blips shouldn't disrupt airflow, but sustained loss should
 *   return to the safe midpoint.
 * - Fan: FORCE_OFF on power loss. On comms loss, HOLD_LAST for 300s
 *   then FORCE_OFF — fan is not safety-critical, brief interruption
 *   is acceptable.
 */

#include "hvac_failsafe.h"
#include "hvac_rules.h"
#include "../../shared/actuator_failsafe.h"
#include <string.h>

/* ---------- Comms-loss timeouts ---------- */

#define HVAC_COMPRESSOR_COMMS_TIMEOUT   30    /* 30 seconds. */
#define HVAC_DAMPER_COMMS_TIMEOUT       300   /* 5 minutes. */
#define HVAC_FAN_COMMS_TIMEOUT          300   /* 5 minutes. */

/* ---------- Fail-safe table ---------- */

static const ActuatorFailSafeEntry_t s_hvac_failsafe_table[] = {
    /**
     * Compressor relay — CRITICAL.
     * Power loss: FORCE_OFF (prevent short-cycling and equipment damage).
     * Comms loss: FORCE_OFF after 30s (uncontrolled compressor is dangerous).
     */
    {
        .actuator_id       = HVAC_ACTUATOR_COMPRESSOR_RELAY,
        .criticality       = ACTUATOR_SAFETY_CRITICAL,
        .power_loss_mode   = FAILSAFE_FORCE_OFF,
        .comms_loss_mode   = FAILSAFE_FORCE_OFF,
        .comms_timeout_sec = HVAC_COMPRESSOR_COMMS_TIMEOUT
    },

    /**
     * Damper motor — STANDARD.
     * Power loss: FORCE_SAFE_POS (50% open).
     *
     * DESIGN DECISION — damper fail-safe direction:
     * Full-closed loses ventilation, risking stale air and CO2 buildup.
     * Full-open wastes conditioned energy (heated/cooled air escapes).
     * 50% open is the safe default — maintains baseline ventilation
     * without excessive energy loss. This is an engineering judgment
     * for typical commercial buildings; specific deployments may require
     * different percentages based on ventilation requirements, building
     * codes, and occupancy patterns.
     *
     * Comms loss: HOLD_LAST for 300s, then FORCE_SAFE_POS (50% open).
     * Brief comms blips shouldn't disrupt airflow, but sustained loss
     * should return to the safe midpoint.
     */
    {
        .actuator_id       = HVAC_ACTUATOR_DAMPER_MOTOR,
        .criticality       = ACTUATOR_SAFETY_STANDARD,
        .power_loss_mode   = FAILSAFE_FORCE_SAFE_POS,
        .comms_loss_mode   = FAILSAFE_HOLD_LAST,
        .comms_timeout_sec = HVAC_DAMPER_COMMS_TIMEOUT
    },

    /**
     * Fan motor — STANDARD.
     * Power loss: FORCE_OFF (fan is not safety-critical).
     * Comms loss: HOLD_LAST for 300s, then FORCE_OFF.
     * Brief interruption is acceptable; sustained loss stops the fan.
     */
    {
        .actuator_id       = HVAC_ACTUATOR_FAN_MOTOR,
        .criticality       = ACTUATOR_SAFETY_STANDARD,
        .power_loss_mode   = FAILSAFE_FORCE_OFF,
        .comms_loss_mode   = FAILSAFE_HOLD_LAST,
        .comms_timeout_sec = HVAC_FAN_COMMS_TIMEOUT
    }
};

#define HVAC_FAILSAFE_COUNT (sizeof(s_hvac_failsafe_table) / sizeof(s_hvac_failsafe_table[0]))

/* ---------- Public API ---------- */

bool hvac_failsafe_register_all(void)
{
    bool success = true;

    for (uint8_t i = 0; i < HVAC_FAILSAFE_COUNT; i++) {
        if (!actuator_failsafe_register(&s_hvac_failsafe_table[i])) {
            success = false;
        }
    }

    return success;
}

uint16_t hvac_get_comms_timeout(uint8_t actuator_id)
{
    for (uint8_t i = 0; i < HVAC_FAILSAFE_COUNT; i++) {
        if (s_hvac_failsafe_table[i].actuator_id == actuator_id) {
            return s_hvac_failsafe_table[i].comms_timeout_sec;
        }
    }
    return 0;
}
