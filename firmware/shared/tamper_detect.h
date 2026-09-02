/**
 * @file tamper_detect.h
 * @brief Tamper switch detection — GPIO-triggered tamper handler.
 *
 * Architecture ref: Section 4, Enhanced tier (mandatory for Industrial).
 * Threat addressed: T3 (physical tamper / JTAG dump).
 *
 * Implements a GPIO-triggered tamper-detect handler with:
 * - Tamper state tracking (TAMPER_NONE / TAMPER_DETECTED)
 * - Audit-log event (AUDIT_TAMPER_DETECTED) on trigger
 * - Mockable GPIO abstraction for testing without hardware
 *
 * Physical wiring is a tracked pre-deployment item.
 */

#ifndef TAMPER_DETECT_H
#define TAMPER_DETECT_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Types ---------- */

/**
 * Tamper detection state.
 */
typedef enum {
    TAMPER_STATE_NONE     = 0,  /**< No tamper detected. */
    TAMPER_STATE_DETECTED = 1   /**< Tamper switch triggered. */
} TamperState_t;

/**
 * Tamper switch configuration.
 */
typedef struct {
    uint8_t  gpio_pin;           /**< GPIO pin number for tamper switch. */
    bool     active_high;        /**< true = active-high (GPIO high = tamper). */
    uint32_t debounce_ms;        /**< Debounce time in milliseconds. */
} TamperConfig_t;

/**
 * GPIO abstraction — mockable for testing without hardware.
 *
 * In production, these map to real GPIO reads.
 * For testing, they can be overridden to simulate tamper events.
 */
typedef struct {
    /** Read the current state of a GPIO pin. Returns true if pin is high. */
    bool (*gpio_read)(uint8_t pin);
} TamperGpio_t;

/* ---------- API ---------- */

/**
 * Initialise the tamper detection subsystem.
 *
 * @param config   Tamper switch configuration.
 * @param gpio     GPIO abstraction (mockable for testing).
 * @return true on success.
 */
bool tamper_detect_init(const TamperConfig_t *config, const TamperGpio_t *gpio);

/**
 * Check the current tamper state.
 * Call this periodically (e.g. in the main loop) or from an ISR.
 *
 * @return Current tamper state.
 */
TamperState_t tamper_detect_get_state(void);

/**
 * Read the tamper switch GPIO directly (bypassing state tracking).
 * Useful for initial state check at boot.
 *
 * @return true if tamper switch is currently triggered.
 */
bool tamper_detect_read_gpio(void);

/**
 * Reset tamper state (e.g. after physical inspection confirms no tamper).
 * This is a privileged operation — in production, it should require
 * authenticated admin access.
 *
 * @return true on success.
 */
bool tamper_detect_reset(void);

/**
 * Check if tamper detection has been initialised.
 *
 * @return true if initialised.
 */
bool tamper_detect_is_initialised(void);

#endif /* TAMPER_DETECT_H */
