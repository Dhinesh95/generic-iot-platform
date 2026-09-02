/**
 * @file water_failsafe.c
 * @brief Water Treatment actuator fail-safe decision table.
 *
 * Architecture ref: Section 10, Water Treatment table.
 *
 * +---------------------+------------+---------------------+----------------------------+-----------+
 * | Actuator            | Criticality| Power-Loss Mode     | Comms-Loss Mode            | Timeout   |
 * +---------------------+------------+---------------------+----------------------------+-----------+
 * | Dosing valve (Cl)   | CRITICAL   | FORCE_OFF           | FORCE_OFF (10s)            | 10s       |
 * | Main supply valve   | CRITICAL   | DE_ENERGIZE (NC)    | HOLD_LAST (60s) → DE_ENERGIZE| 60s     |
 * | Circulation pump    | STANDARD   | FORCE_OFF           | HOLD_LAST (120s) → FORCE_OFF| 120s     |
 * +---------------------+------------+---------------------+----------------------------+-----------+
 *
 * CRITICAL DESIGN PRINCIPLE (Phase 5.2 precedent):
 * Equipment-protection and chemical-safety mechanisms default to the
 * safe/conservative behavior under uncertainty, not the available/
 * permissive one. This is the OPPOSITE trade-off from a life-safety
 * egress lock (e.g. Home door lock, SR-015), where availability
 * (egress) is prioritized over restriction.
 *
 * For the dosing valve specifically:
 * - FAILING CLOSED (no dosing) when uncertain prevents over-dose
 *   (chemical exposure risk to end users). Under-dose is also a risk
 *   (insufficient disinfection), but over-dose is the more immediately
 *   dangerous failure mode. A brief interruption in dosing allows
 *   existing residual chlorine to maintain protection; a brief
 *   over-dose has no such grace period.
 * - This is NOT an engineering default that can be silently flipped —
 *   it requires explicit justification per deployment.
 */

#include "water_failsafe.h"
#include "water_rules.h"
#include "../../shared/actuator_failsafe.h"
#include <string.h>

/* ---------- Comms-loss timeouts ---------- */

#define WATER_DOSING_VALVE_COMMS_TIMEOUT    10    /* 10 seconds — unmonitored dosing is dangerous. */
#define WATER_MAIN_VALVE_COMMS_TIMEOUT      60    /* 60 seconds — brief blips shouldn't cut supply. */
#define WATER_CIRC_PUMP_COMMS_TIMEOUT       120   /* 120 seconds — brief blip tolerable. */

/* ---------- Fail-safe table ---------- */

static const ActuatorFailSafeEntry_t s_water_failsafe_table[] = {
    /**
     * Dosing valve (chlorine) — CRITICAL.
     *
     * Power loss: FORCE_OFF (stop dosing immediately).
     * Comms loss: FORCE_OFF after 10s (unmonitored dosing risks over-dose).
     *
     * DESIGN DECISION — dosing valve fail-safe direction:
     * FAILING CLOSED (no dosing) is the conservative default under
     * uncertainty. This follows the Phase 5.2 principle: equipment/
     * chemical-safety mechanisms default to the safe/conservative
     * behavior under uncertainty.
     *
     * Rationale for FAIL-CLOSED over FAIL-OPEN:
     * - Over-dose (fail-open): chlorine exposure risk to end users.
     *   At >4.0 ppm (EPA MRDL), skin/eye irritation; at >10 ppm,
     *   acute poisoning risk. No grace period — immediate harm.
     * - Under-dose (fail-closed): reduced disinfection effectiveness.
     *   Existing residual chlorine in the distribution system provides
     *   a buffer (typically hours of protection). Brief interruption
     *   in dosing is recoverable; brief over-dose is not.
     * - Therefore: FAIL-CLOSED is the safer default when uncertain.
     *
     * This is a chemical-safety decision, not just equipment protection.
     * It requires explicit justification per deployment and should be
     * reviewed by a water-safety engineer before field use.
     */
    {
        .actuator_id       = WATER_ACTUATOR_DOSING_VALVE,
        .criticality       = ACTUATOR_SAFETY_CRITICAL,
        .power_loss_mode   = FAILSAFE_FORCE_OFF,
        .comms_loss_mode   = FAILSAFE_FORCE_OFF,
        .comms_timeout_sec = WATER_DOSING_VALVE_COMMS_TIMEOUT
    },

    /**
     * Main supply valve — CRITICAL.
     *
     * Power loss: DE_ENERGIZE (normally-closed spring-return valve).
     *   On power loss, the valve springs shut, stopping water supply.
     *   This is the conservative default — no unmonitored water flow.
     * Comms loss: HOLD_LAST for 60s, then DE_ENERGIZE.
     *   Brief comms blips shouldn't cut supply to consumers, but
     *   sustained loss should stop unmonitored flow to prevent
     *   potential contamination or overflow.
     */
    {
        .actuator_id       = WATER_ACTUATOR_MAIN_SUPPLY_VALVE,
        .criticality       = ACTUATOR_SAFETY_CRITICAL,
        .power_loss_mode   = FAILSAFE_DE_ENERGIZE,
        .comms_loss_mode   = FAILSAFE_HOLD_LAST,
        .comms_timeout_sec = WATER_MAIN_VALVE_COMMS_TIMEOUT
    },

    /**
     * Circulation pump — STANDARD.
     *
     * Power loss: FORCE_OFF (stop pump immediately).
     * Comms loss: HOLD_LAST for 120s, then FORCE_OFF.
     *   Brief comms blips shouldn't interrupt circulation (which
     *   maintains water quality through mixing and distribution),
     *   but sustained loss should stop uncontrolled pump operation.
     *   The 120s timeout is longer than the dosing valve (10s)
     *   because a brief pump interruption is less dangerous than
     *   a brief dosing interruption.
     */
    {
        .actuator_id       = WATER_ACTUATOR_CIRCULATION_PUMP,
        .criticality       = ACTUATOR_SAFETY_STANDARD,
        .power_loss_mode   = FAILSAFE_FORCE_OFF,
        .comms_loss_mode   = FAILSAFE_HOLD_LAST,
        .comms_timeout_sec = WATER_CIRC_PUMP_COMMS_TIMEOUT
    }
};

#define WATER_FAILSAFE_COUNT (sizeof(s_water_failsafe_table) / sizeof(s_water_failsafe_table[0]))

/* ---------- Public API ---------- */

bool water_failsafe_register_all(void)
{
    bool success = true;

    for (uint8_t i = 0; i < WATER_FAILSAFE_COUNT; i++) {
        if (!actuator_failsafe_register(&s_water_failsafe_table[i])) {
            success = false;
        }
    }

    return success;
}

uint16_t water_get_comms_timeout(uint8_t actuator_id)
{
    for (uint8_t i = 0; i < WATER_FAILSAFE_COUNT; i++) {
        if (s_water_failsafe_table[i].actuator_id == actuator_id) {
            return s_water_failsafe_table[i].comms_timeout_sec;
        }
    }
    return 0;
}
