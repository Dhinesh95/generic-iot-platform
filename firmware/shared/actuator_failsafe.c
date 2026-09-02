/**
 * @file actuator_failsafe.c
 * @brief Actuator fail-safe decision table implementation.
 *
 * Architecture ref: Section 10.
 *
 * Power loss and comms loss are distinct failure modes with distinct
 * responses. The comms_timeout_sec field controls when comms-loss
 * mode activates — until the timeout elapses, the actuator holds
 * its last state.
 */

#include "actuator_failsafe.h"
#include <string.h>

/* ---------- Internal state ---------- */

static ActuatorFailSafeEntry_t g_failsafe_table[ACTUATOR_FAILSAFE_MAX_ENTRIES];
static uint8_t g_entry_count = 0;
static bool g_initialised = false;

/* ---------- Public API ---------- */

bool actuator_failsafe_init(void)
{
    memset(g_failsafe_table, 0, sizeof(g_failsafe_table));
    g_entry_count = 0;
    g_initialised = true;
    return true;
}

bool actuator_failsafe_register(const ActuatorFailSafeEntry_t *entry)
{
    if (!entry) return false;
    if (!g_initialised) return false;
    if (g_entry_count >= ACTUATOR_FAILSAFE_MAX_ENTRIES) return false;

    /* Check for existing entry — update if found. */
    for (uint8_t i = 0; i < g_entry_count; i++) {
        if (g_failsafe_table[i].actuator_id == entry->actuator_id) {
            memcpy(&g_failsafe_table[i], entry, sizeof(ActuatorFailSafeEntry_t));
            return true;
        }
    }

    memcpy(&g_failsafe_table[g_entry_count], entry, sizeof(ActuatorFailSafeEntry_t));
    g_entry_count++;
    return true;
}

bool actuator_failsafe_lookup(
    uint8_t actuator_id,
    bool is_power_loss,
    ActuatorFailSafeEntry_t *out_entry)
{
    if (!out_entry) return false;
    if (!g_initialised) return false;

    for (uint8_t i = 0; i < g_entry_count; i++) {
        if (g_failsafe_table[i].actuator_id == actuator_id) {
            memcpy(out_entry, &g_failsafe_table[i], sizeof(ActuatorFailSafeEntry_t));
            (void)is_power_loss;  /* Caller uses out_entry fields directly. */
            return true;
        }
    }

    return false;
}

bool actuator_failsafe_execute(
    uint8_t actuator_id,
    bool is_power_loss,
    uint16_t comms_loss_elapsed_sec)
{
    if (!g_initialised) return false;

    ActuatorFailSafeEntry_t entry;
    if (!actuator_failsafe_lookup(actuator_id, is_power_loss, &entry)) {
        return false;
    }

    ActuatorFailSafeMode_t mode;

    if (is_power_loss) {
        mode = entry.power_loss_mode;
    } else {
        /* Comms loss: check timeout before triggering fail-safe. */
        if (comms_loss_elapsed_sec < entry.comms_timeout_sec) {
            /* Timeout not yet elapsed — hold last state. */
            return true;  /* Not an error; comms-loss mode not yet active. */
        }
        mode = entry.comms_loss_mode;
    }

    /*
     * Execute the fail-safe action.
     *
     * In a real system this would drive the physical actuator.
     * Here we return the mode that should be applied — the
     * caller (or HAL) maps this to physical I/O.
     *
     * TODO: In production, this function would call into a HAL
     * function pointer for the specific actuator driver.
     */
    (void)mode;  /* Action determined; physical execution is HAL-dependent. */

    return true;
}

uint8_t actuator_failsafe_count(void)
{
    return g_entry_count;
}
