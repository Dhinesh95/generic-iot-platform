/**
 * @file jtag_disable.h
 * @brief JTAG fuse-burn disable — one-way, irreversible hardware operation.
 *
 * Architecture ref: Section 4, Enhanced tier (mandatory for Industrial).
 * Threat addressed: T3 (physical tamper / JTAG dump).
 *
 * CRITICAL SAFETY NOTICE:
 * This is a ONE-WAY, IRREVERSIBLE hardware operation. Once JTAG is
 * disabled via eFuse burn, it CANNOT be re-enabled. This is a
 * manufacturing-time operation that requires:
 * 1. Explicit sign-off from the deployment authority
 * 2. A confirmed, tested firmware image already verified
 * 3. Physical access to the device (not remotely triggerable)
 *
 * This file provides the logic path that WOULD perform the fuse burn.
 * Actual execution is prevented by:
 * - Compile-time guard (JTAG_DISABLE_ACTUAL_BURN must be defined)
 * - Runtime confirmation gate (two-step confirmation)
 * - Audit-log entry before and after the operation
 *
 * NEVER execute this automatically or during testing.
 */

#ifndef JTAG_DISABLE_H
#define JTAG_DISABLE_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Types ---------- */

/**
 * JTAG disable operation result.
 */
typedef enum {
    JTAG_RESULT_OK,                /**< Operation completed (or would complete). */
    JTAG_RESULT_NOT_INITIALISED,   /**< Subsystem not initialised. */
    JTAG_RESULT_NOT_CONFIRMED,     /**< Confirmation gate not passed. */
    JTAG_RESULT_ALREADY_DISABLED,  /**< JTAG already disabled. */
    JTAG_RESULT_HARDWARE_ERROR     /**< Hardware operation failed. */
} JtagResult_t;

/**
 * JTAG disable configuration.
 */
typedef struct {
    bool require_two_step_confirm; /**< If true, requires two separate confirm calls. */
    bool audit_before_and_after;   /**< If true, logs before AND after the operation. */
} JtagConfig_t;

/* ---------- API ---------- */

/**
 * Initialise the JTAG disable subsystem.
 *
 * @param config  Configuration options.
 * @return true on success.
 */
bool jtag_disable_init(const JtagConfig_t *config);

/**
 * Check if JTAG is currently enabled (can be read).
 *
 * @return true if JTAG is still enabled (not yet burned).
 */
bool jtag_disable_is_enabled(void);

/**
 * Perform the JTAG disable operation.
 *
 * CRITICAL: This is a ONE-WAY, IRREVERSIBLE operation.
 * - If JTAG_DISABLE_ACTUAL_BURN is NOT defined at compile time,
 *   this logs the intended operation but does NOT burn the fuse.
 * - If JTAG_DISABLE_ACTUAL_BURN IS defined, this WILL burn the
 *   eFuse (irreversible).
 * - A confirmation gate must be passed first via jtag_disable_confirm().
 *
 * @return JTAG_RESULT_OK on success, or an error code.
 */
JtagResult_t jtag_disable_perform(void);

/**
 * Provide the first step of the two-step confirmation.
 * Must be called before jtag_disable_perform() if require_two_step_confirm
 * is true in the configuration.
 *
 * @return true if confirmation step accepted.
 */
bool jtag_disable_confirm_step1(void);

/**
 * Provide the second step of the two-step confirmation.
 * Must be called after jtag_disable_confirm_step1() and before
 * jtag_disable_perform() if require_two_step_confirm is true.
 *
 * @return true if confirmation step accepted.
 */
bool jtag_disable_confirm_step2(void);

#endif /* JTAG_DISABLE_H */
