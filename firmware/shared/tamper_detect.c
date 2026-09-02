/**
 * @file tamper_detect.c
 * @brief Tamper switch detection implementation.
 *
 * Architecture ref: Section 4, Enhanced tier (mandatory for Industrial).
 *
 * Uses a mockable GPIO abstraction so the detection/audit-log/state-
 * transition logic is real and testable without physical hardware.
 * Physical wiring is a tracked pre-deployment item.
 */

#include "tamper_detect.h"
#include "audit_log.h"
#include <string.h>

/* ---------- Internal state ---------- */

static TamperConfig_t  s_config;
static TamperGpio_t    s_gpio;
static TamperState_t   s_state = TAMPER_STATE_NONE;
static bool            s_initialised = false;

/* ---------- Public API ---------- */

bool tamper_detect_init(const TamperConfig_t *config, const TamperGpio_t *gpio)
{
    if (!config || !gpio || !gpio->gpio_read) return false;

    memcpy(&s_config, config, sizeof(TamperConfig_t));
    memcpy(&s_gpio, gpio, sizeof(TamperGpio_t));
    s_state = TAMPER_STATE_NONE;
    s_initialised = true;

    /* Check initial state — if tamper switch is already triggered at boot,
     * record it immediately. This catches tamper-before-boot scenarios. */
    if (tamper_detect_read_gpio()) {
        s_state = TAMPER_STATE_DETECTED;
    }

    return true;
}

TamperState_t tamper_detect_get_state(void)
{
    if (!s_initialised) return TAMPER_STATE_NONE;

    /* Read GPIO and update state. */
    if (tamper_detect_read_gpio()) {
        if (s_state != TAMPER_STATE_DETECTED) {
            /* Transition to detected — log to audit trail. */
            s_state = TAMPER_STATE_DETECTED;
            audit_log_add(AUDIT_TAMPER_DETECTED, 0, 0,
                         "Physical tamper switch triggered");
        }
    }

    return s_state;
}

bool tamper_detect_read_gpio(void)
{
    if (!s_initialised || !s_gpio.gpio_read) return false;

    bool raw = s_gpio.gpio_read(s_config.gpio_pin);

    /* Apply active-high/active-low logic. */
    if (s_config.active_high) {
        return raw;
    } else {
        return !raw;
    }
}

bool tamper_detect_reset(void)
{
    if (!s_initialised) return false;

    /* Only allow reset if the tamper switch is no longer physically triggered.
     * This prevents software-only reset while physical tamper is active. */
    if (tamper_detect_read_gpio()) {
        return false;  /* Physical tamper still active — cannot reset. */
    }

    s_state = TAMPER_STATE_NONE;
    return true;
}

bool tamper_detect_is_initialised(void)
{
    return s_initialised;
}
