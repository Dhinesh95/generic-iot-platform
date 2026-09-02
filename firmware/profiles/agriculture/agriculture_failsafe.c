/**
 * @file agriculture_failsafe.c
 * @brief Agriculture actuator fail-safe decision table.
 *
 * Architecture ref: Section 10, Agriculture table.
 *
 * +------------------+------------+------------------+------------------+-----------+
 * | Actuator         | Criticality| Power-Loss Mode  | Comms-Loss Mode  | Timeout   |
 * +------------------+------------+------------------+------------------+-----------+
 * | Irrigation pump  | CRITICAL   | FORCE_OFF        | FORCE_OFF (30s)  | 30s       |
 * | Irrigation valve | OPERATIONAL| DE_ENERGIZE (NC) | HOLD_LAST (600s) | 600s      |
 * | Frost valve      | OPERATIONAL| DE_ENERGIZE (NC) | HOLD_LAST (300s) | 300s      |
 * +------------------+------------+------------------+------------------+-----------+
 *
 * Reasoning:
 * - Pump: FORCE_OFF on both power and comms loss. Running a pump with
 *   no monitoring risks dry-run damage. Brief comms blips should not
 *   keep an unmonitored pump running.
 * - Valve (normally-closed): DE_ENERGIZE on power loss (spring-shut).
 *   On comms loss, HOLD_LAST for 600s then DE_ENERGIZE — brief blips
 *   shouldn't interrupt irrigation, but sustained loss should stop it
 *   to prevent water waste.
 * - Frost valve: similar to irrigation valve but shorter timeout.
 */

#include "agriculture_failsafe.h"
#include "agriculture_rules.h"
#include "../../shared/actuator_failsafe.h"
#include <string.h>

/* ---------- Comms-loss timeouts ---------- */

#define AGRI_PUMP_COMMS_TIMEOUT       30    /* 30 seconds. */
#define AGRI_VALVE_COMMS_TIMEOUT      600   /* 10 minutes. */
#define AGRI_FROST_VALVE_COMMS_TIMEOUT 300  /* 5 minutes. */

/* ---------- Fail-safe table ---------- */

static const ActuatorFailSafeEntry_t s_agriculture_failsafe_table[] = {
    /**
     * Irrigation pump — CRITICAL.
     * Power loss: FORCE_OFF (prevent dry-run damage).
     * Comms loss: FORCE_OFF after 30s (unmonitored pump is dangerous).
     */
    {
        .actuator_id       = AGRI_ACTUATOR_IRRIGATION_PUMP,
        .criticality       = ACTUATOR_SAFETY_CRITICAL,
        .power_loss_mode   = FAILSAFE_FORCE_OFF,
        .comms_loss_mode   = FAILSAFE_FORCE_OFF,
        .comms_timeout_sec = AGRI_PUMP_COMMS_TIMEOUT
    },

    /**
     * Irrigation valve — OPERATIONAL.
     * Power loss: DE_ENERGIZE (normally-closed spring-return valve).
     *   On power loss, the valve springs shut. This stops irrigation
     *   but wastes no water. Alternative (fail-open) would protect
     *   crops during outage but waste water. Conservative default.
     * Comms loss: HOLD_LAST for 600s, then DE_ENERGIZE.
     *   Brief comms blips shouldn't interrupt irrigation schedule.
     */
    {
        .actuator_id       = AGRI_ACTUATOR_IRRIGATION_VALVE,
        .criticality       = ACTUATOR_SAFETY_STANDARD,
        .power_loss_mode   = FAILSAFE_DE_ENERGIZE,
        .comms_loss_mode   = FAILSAFE_HOLD_LAST,
        .comms_timeout_sec = AGRI_VALVE_COMMS_TIMEOUT
    },

    /**
     * Frost protection valve — OPERATIONAL.
     * Power loss: DE_ENERGIZE (normally-closed).
     *   Frost protection is secondary to water conservation.
     *   Without power, the valve shuts. If frost is imminent,
     *   this is a trade-off: crop damage risk vs water waste.
     * Comms loss: HOLD_LAST for 300s, then DE_ENERGIZE.
     */
    {
        .actuator_id       = AGRI_ACTUATOR_FROST_VALVE,
        .criticality       = ACTUATOR_SAFETY_STANDARD,
        .power_loss_mode   = FAILSAFE_DE_ENERGIZE,
        .comms_loss_mode   = FAILSAFE_HOLD_LAST,
        .comms_timeout_sec = AGRI_FROST_VALVE_COMMS_TIMEOUT
    }
};

#define AGRI_FAILSAFE_COUNT (sizeof(s_agriculture_failsafe_table) / sizeof(s_agriculture_failsafe_table[0]))

/* ---------- Public API ---------- */

bool agriculture_failsafe_register_all(void)
{
    bool success = true;

    for (uint8_t i = 0; i < AGRI_FAILSAFE_COUNT; i++) {
        if (!actuator_failsafe_register(&s_agriculture_failsafe_table[i])) {
            success = false;
        }
    }

    return success;
}

uint16_t agriculture_get_comms_timeout(uint8_t actuator_id)
{
    for (uint8_t i = 0; i < AGRI_FAILSAFE_COUNT; i++) {
        if (s_agriculture_failsafe_table[i].actuator_id == actuator_id) {
            return s_agriculture_failsafe_table[i].comms_timeout_sec;
        }
    }
    return 0;
}
