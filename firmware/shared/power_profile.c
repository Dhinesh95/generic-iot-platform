/**
 * @file power_profile.c
 * @brief Power profile mechanism implementation.
 *
 * Architecture ref: Section 8.
 *
 * This is a CORE mechanism, not Agriculture-specific.
 */

#include "power_profile.h"
#include <string.h>

static PowerProfile_t g_profiles[POWER_PROFILE_MAX_PROFILES];
static uint8_t g_profile_count = 0;
static bool g_initialised = false;

bool power_profile_init(void)
{
    memset(g_profiles, 0, sizeof(g_profiles));
    g_profile_count = 0;
    g_initialised = true;
    return true;
}

bool power_profile_register(const PowerProfile_t *profile)
{
    if (!profile || !g_initialised) return false;
    if (g_profile_count >= POWER_PROFILE_MAX_PROFILES) return false;

    memcpy(&g_profiles[g_profile_count], profile, sizeof(PowerProfile_t));
    g_profile_count++;
    return true;
}

const PowerProfile_t *power_profile_get(uint8_t index)
{
    if (!g_initialised || index >= g_profile_count) return NULL;
    return &g_profiles[index];
}

PowerAction_t power_profile_evaluate(
    PowerState_t *state,
    const PowerProfile_t *profile,
    uint64_t current_ms,
    uint16_t battery_mv)
{
    if (!state || !profile) return POWER_ACTION_STAY_ACTIVE;
    if (!g_initialised) return POWER_ACTION_STAY_ACTIVE;
    (void)current_ms;  /* Used in production for time-based decisions. */

    state->battery_mv = battery_mv;

    /* Use the active profile's thresholds — not hardcoded constants. */
    if (battery_mv > 0 && profile->critical_battery_mv > 0) {
        if (battery_mv < profile->critical_battery_mv) {
            state->critical_battery = true;
            state->low_battery = true;
            return POWER_ACTION_SHUTDOWN;
        }
        if (battery_mv < profile->low_battery_mv) {
            state->low_battery = true;
            return POWER_ACTION_THROTTLE;
        }
        state->low_battery = false;
        state->critical_battery = false;
    }

    return POWER_ACTION_STAY_ACTIVE;
}

uint8_t power_profile_count(void)
{
    return g_profile_count;
}

PowerProfile_t power_profile_create_mains(void)
{
    PowerProfile_t p;
    memset(&p, 0, sizeof(p));
    p.source = POWER_SOURCE_MAINS;
    p.active_duration_ms = 0;       /* Indefinite. */
    p.sleep_duration_ms = 0;        /* No sleep. */
    p.low_battery_mv = 0;           /* No battery monitoring. */
    p.critical_battery_mv = 0;
    p.deep_sleep_enabled = false;
    p.telemetry_duty_cycle = false;
    return p;
}

PowerProfile_t power_profile_create_battery(
    uint32_t active_ms, uint32_t sleep_ms, uint16_t low_battery_mv)
{
    PowerProfile_t p;
    memset(&p, 0, sizeof(p));
    p.source = POWER_SOURCE_BATTERY;
    p.active_duration_ms = active_ms;
    p.sleep_duration_ms = sleep_ms;
    p.low_battery_mv = low_battery_mv;
    p.critical_battery_mv = (low_battery_mv > 200) ? (low_battery_mv - 200) : 2800;
    p.deep_sleep_enabled = true;
    p.telemetry_duty_cycle = true;
    return p;
}

PowerProfile_t power_profile_create_solar(
    uint32_t active_ms, uint32_t sleep_ms, uint16_t low_battery_mv)
{
    PowerProfile_t p;
    memset(&p, 0, sizeof(p));
    p.source = POWER_SOURCE_SOLAR;
    p.active_duration_ms = active_ms;
    p.sleep_duration_ms = sleep_ms;
    p.low_battery_mv = low_battery_mv;
    p.critical_battery_mv = (low_battery_mv > 200) ? (low_battery_mv - 200) : 2800;
    p.deep_sleep_enabled = true;
    p.telemetry_duty_cycle = true;
    return p;
}
