/**
 * @file home_failsafe.c
 * @brief Home/Building actuator fail-safe decision table.
 *
 * Architecture ref: Section 10, Home/Building table.
 *
 * +------------------+------------+------------------+------------------+-----------+
 * | Actuator         | Criticality| Power-Loss Mode  | Comms-Loss Mode  | Timeout   |
 * +------------------+------------+------------------+------------------+-----------+
 * | Electronic lock  | CRITICAL   | Fail-unlocked*   | HOLD_LAST        | —         |
 * | Irrigation valve | OPERATIONAL| FORCE_OFF        | HOLD_LAST        | 600s      |
 * | Light relay      | OPERATIONAL| FORCE_OFF        | HOLD_LAST        | indefinite|
 * +------------------+------------+------------------+------------------+-----------+
 *
 * * COMPLIANCE NOTE for door lock:
 *   Fail-unlocked-on-power-loss is a REGULATORY/COMPLIANCE decision
 *   for the deployment jurisdiction, NOT a hardcoded engineering default.
 *   See detailed TODO in the implementation below.
 */

#include "home_failsafe.h"
#include "home_rules.h"
#include "../../shared/actuator_failsafe.h"
#include <string.h>

/* ---------- Comms-loss timeouts ---------- */

#define HOME_DOOR_LOCK_COMMS_TIMEOUT      0     /* HOLD_LAST indefinitely. */
#define HOME_IRRIGATION_COMMS_TIMEOUT     600   /* 10 minutes. */
#define HOME_LIGHT_RELAY_COMMS_TIMEOUT    0     /* HOLD_LAST indefinitely. */

/* ---------- Fail-safe table ---------- */

static const ActuatorFailSafeEntry_t s_home_failsafe_table[] = {
    /**
     * Electronic door lock — CRITICAL.
     *
     * Power loss: FAILSAFE_FORCE_SAFE_POS.
     *
     * TODO — COMPLIANCE/REGULATORY SIGN-OFF REQUIRED:
     * The fail-unlocked-on-power-loss direction for electronic door locks
     * is NOT a purely engineering decision. Fire/egress codes in most
     * jurisdictions require electronic locks to fail unlocked on power
     * loss to allow safe egress during emergencies. However:
     *
     *   1. The specific direction (locked vs unlocked) MUST be determined
     *      by compliance/legal review for the deployment jurisdiction.
     *   2. Some security-focused deployments may require fail-locked
     *      (with mechanical override) — this is a jurisdiction-specific
     *      trade-off between fire safety and physical security.
     *   3. This is CONFIGURABLE at build-time via a compile flag:
     *      -DHOME_DOOR_LOCK_FAIL_UNLOCKED (default: enabled, matching
     *      the most common regulatory requirement).
     *
     * Verification status: Pending compliance/legal sign-off.
     * This is NOT "Verified" — engineering correctness and regulatory
     * sign-off are different things and must not be conflated.
     */
    {
        .actuator_id       = HOME_ACTUATOR_DOOR_LOCK,
        .criticality       = ACTUATOR_SAFETY_CRITICAL,
        .power_loss_mode   = FAILSAFE_FORCE_SAFE_POS,
        .comms_loss_mode   = FAILSAFE_HOLD_LAST,
        .comms_timeout_sec = HOME_DOOR_LOCK_COMMS_TIMEOUT
    },

    /**
     * Irrigation valve — OPERATIONAL.
     * Power loss: FORCE_OFF (water conservation, low safety stakes).
     * Comms loss: HOLD_LAST for 600s, then FORCE_OFF.
     */
    {
        .actuator_id       = HOME_ACTUATOR_IRRIGATION_VALVE,
        .criticality       = ACTUATOR_SAFETY_STANDARD,
        .power_loss_mode   = FAILSAFE_FORCE_OFF,
        .comms_loss_mode   = FAILSAFE_HOLD_LAST,
        .comms_timeout_sec = HOME_IRRIGATION_COMMS_TIMEOUT
    },

    /**
     * Light relay — OPERATIONAL.
     * Power loss: FORCE_OFF.
     * Comms loss: HOLD_LAST indefinitely (no safety implication either way).
     */
    {
        .actuator_id       = HOME_ACTUATOR_LIGHT_RELAY,
        .criticality       = ACTUATOR_SAFETY_STANDARD,
        .power_loss_mode   = FAILSAFE_FORCE_OFF,
        .comms_loss_mode   = FAILSAFE_HOLD_LAST,
        .comms_timeout_sec = HOME_LIGHT_RELAY_COMMS_TIMEOUT
    }
};

#define HOME_FAILSAFE_COUNT (sizeof(s_home_failsafe_table) / sizeof(s_home_failsafe_table[0]))

/* ---------- Public API ---------- */

bool home_failsafe_register_all(void)
{
    bool success = true;

    for (uint8_t i = 0; i < HOME_FAILSAFE_COUNT; i++) {
        if (!actuator_failsafe_register(&s_home_failsafe_table[i])) {
            success = false;
        }
    }

    return success;
}

uint16_t home_get_comms_timeout(uint8_t actuator_id)
{
    for (uint8_t i = 0; i < HOME_FAILSAFE_COUNT; i++) {
        if (s_home_failsafe_table[i].actuator_id == actuator_id) {
            return s_home_failsafe_table[i].comms_timeout_sec;
        }
    }
    return 0;
}
