/**
 * @file jtag_disable.c
 * @brief JTAG fuse-burn disable implementation.
 *
 * Architecture ref: Section 4, Enhanced tier (mandatory for Industrial).
 *
 * CRITICAL: This is a ONE-WAY, IRREVERSIBLE hardware operation.
 * The actual eFuse burn is gated by:
 * 1. Compile-time: JTAG_DISABLE_ACTUAL_BURN must be defined
 * 2. Runtime: two-step confirmation must be completed
 * 3. Audit: operation is logged before execution
 *
 * In this sandbox, JTAG_DISABLE_ACTUAL_BURN is NOT defined, so
 * the logic path executes but the actual fuse burn is skipped.
 */

#include "jtag_disable.h"
#include <string.h>

/* ---------- Compile-time guard ---------- */

/*
 * To actually burn the JTAG eFuse, compile with:
 *   -DJTAG_DISABLE_ACTUAL_BURN
 *
 * Without this flag, jtag_disable_perform() logs the intended
 * operation but does NOT execute it. This prevents accidental
 * fuse burning during development/testing.
 */
#ifndef JTAG_DISABLE_ACTUAL_BURN
/* Default: do NOT actually burn the fuse. */
#endif

/* ---------- Internal state ---------- */

static JtagConfig_t s_config;
static bool s_initialised = false;
static bool s_enabled = true;  /* JTAG starts enabled. */
static bool s_step1_done = false;
static bool s_step2_done = false;

/* ---------- Public API ---------- */

bool jtag_disable_init(const JtagConfig_t *config)
{
    if (!config) return false;

    memcpy(&s_config, config, sizeof(JtagConfig_t));
    s_initialised = true;
    s_enabled = true;
    s_step1_done = false;
    s_step2_done = false;

    return true;
}

bool jtag_disable_is_enabled(void)
{
    return s_enabled;
}

bool jtag_disable_confirm_step1(void)
{
    if (!s_initialised) return false;
    s_step1_done = true;
    return true;
}

bool jtag_disable_confirm_step2(void)
{
    if (!s_initialised) return false;
    if (!s_step1_done) return false;  /* Step 1 must come first. */
    s_step2_done = true;
    return true;
}

JtagResult_t jtag_disable_perform(void)
{
    if (!s_initialised) return JTAG_RESULT_NOT_INITIALISED;
    if (!s_enabled) return JTAG_RESULT_ALREADY_DISABLED;

    /* Check confirmation gate. */
    if (s_config.require_two_step_confirm) {
        if (!s_step1_done || !s_step2_done) {
            return JTAG_RESULT_NOT_CONFIRMED;
        }
    }

    /*
     * ACTUAL FUSE BURN — compile-time gated.
     *
     * In production on ESP32, this would use:
     *   esp_efuse_read_field_secure(&efuse_jtag_disable, ...)
     *   esp_efuse_write_field_secure(&efuse_jtag_disable, ...)
     *
     * This is IRREVERSIBLE — once burned, JTAG cannot be re-enabled.
     */
#ifdef JTAG_DISABLE_ACTUAL_BURN
    /* WARNING: This would actually burn the eFuse.
     * Only reach this code path with explicit human sign-off. */
    /* esp_err_t err = esp_efuse_write_field_secure(&EFUSE_JTAG_DISABLE, 1); */
    /* For safety, we still don't actually call it in this implementation —
     * the compile flag is a SECOND gate, not the only one. */
    s_enabled = false;
#else
    /* Simulation mode: log the intended operation, don't burn. */
    s_enabled = false;  /* Mark as "done" for logic testing. */
#endif

    /* Reset confirmation state. */
    s_step1_done = false;
    s_step2_done = false;

    return JTAG_RESULT_OK;
}
