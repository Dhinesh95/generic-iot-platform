/**
 * @file actuator_failsafe.h
 * @brief Actuator fail-safe decision tables (per domain).
 *
 * Architecture ref: Section 10.
 *
 * Power loss and comms loss are distinct scenarios requiring distinct
 * responses — this distinction is maintained explicitly via separate
 * mode fields and a configurable comms_timeout_sec.
 *
 * For life-safety-adjacent actuators (e.g. door locks), the fail-safe
 * direction must be signed off by compliance/legal review for the
 * deployment jurisdiction — this is NOT solely an engineering decision.
 */

#ifndef ACTUATOR_FAILSAFE_H
#define ACTUATOR_FAILSAFE_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Constants ---------- */

#define ACTUATOR_FAILSAFE_MAX_ENTRIES  16  /**< Maximum actuators in fail-safe table. */

/* ---------- Types ---------- */

/**
 * Fail-safe modes for actuators.
 *
 * FAILSAFE_DE_ENERGIZE  — actuator goes to its de-energized physical state.
 * FAILSAFE_HOLD_LAST    — retains last commanded state (requires local energy storage).
 * FAILSAFE_FORCE_OFF    — forced OFF regardless of last state.
 * FAILSAFE_FORCE_SAFE_POS — driven to a specific safe position, not just de-energized.
 */
typedef enum {
    FAILSAFE_DE_ENERGIZE    = 0,
    FAILSAFE_HOLD_LAST      = 1,
    FAILSAFE_FORCE_OFF      = 2,
    FAILSAFE_FORCE_SAFE_POS = 3
} ActuatorFailSafeMode_t;

/**
 * Safety criticality classification for actuators.
 * Aligns with Section 6 rule tiers.
 */
typedef enum {
    ACTUATOR_SAFETY_CRITICAL  = 0,  /**< Life-safety or property-protection actuator. */
    ACTUATOR_SAFETY_STANDARD  = 1   /**< Operational actuator, no immediate safety impact. */
} ActuatorSafetyClass_t;

/**
 * Fail-safe entry for a single actuator.
 *
 * Distinguishes power_loss_mode from comms_loss_mode with a
 * configurable comms_timeout_sec — these two failure modes must
 * NOT be conflated into one.
 */
typedef struct {
    uint8_t                actuator_id;
    ActuatorSafetyClass_t  criticality;       /* Aligns with Section 6 rule tiers. */
    ActuatorFailSafeMode_t power_loss_mode;   /* Hub/Gateway/actuator power lost. */
    ActuatorFailSafeMode_t comms_loss_mode;   /* Comms to Hub lost, actuator still powered. */
    uint16_t               comms_timeout_sec; /* Delay before comms_loss_mode triggers. */
} ActuatorFailSafeEntry_t;

/* ---------- API ---------- */

/**
 * Initialise the actuator fail-safe subsystem.
 *
 * @return true on success.
 */
bool actuator_failsafe_init(void);

/**
 * Look up the fail-safe mode for an actuator.
 *
 * @param actuator_id    The actuator to look up.
 * @param is_power_loss   true for power-loss scenario, false for comms-loss.
 * @param out_entry       Output: the full fail-safe entry (if found).
 * @return true if a matching entry was found.
 */
bool actuator_failsafe_lookup(
    uint8_t actuator_id,
    bool is_power_loss,
    ActuatorFailSafeEntry_t *out_entry
);

/**
 * Register a fail-safe entry for an actuator.
 *
 * @param entry  The fail-safe entry to register.
 * @return true on success.
 */
bool actuator_failsafe_register(const ActuatorFailSafeEntry_t *entry);

/**
 * Execute the fail-safe action for an actuator.
 *
 * @param actuator_id    The actuator.
 * @param is_power_loss   true for power-loss, false for comms-loss.
 * @param comms_loss_elapsed_sec  Seconds since comms was lost (for timeout check).
 * @return true if the fail-safe action was executed.
 */
bool actuator_failsafe_execute(
    uint8_t actuator_id,
    bool is_power_loss,
    uint16_t comms_loss_elapsed_sec
);

/**
 * Get the total number of registered fail-safe entries.
 *
 * @return Number of entries.
 */
uint8_t actuator_failsafe_count(void);

#endif /* ACTUATOR_FAILSAFE_H */
