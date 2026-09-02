/**
 * @file rule_engine_core.h
 * @brief Rule engine core — generic evaluation with vtable dispatch.
 *
 * Architecture ref: Section 6, Safety / Rule Logic Architecture.
 *
 * Core (generic) components live here. Domain-specific data
 * (thresholds, actuator mappings, which rules are safety-locked)
 * are supplied through the DomainProfileVTable_t interface.
 *
 * The RuleEntry_t is exactly 16 bytes packed, including the
 * rule_class field from the start.
 */

#ifndef RULE_ENGINE_CORE_H
#define RULE_ENGINE_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---------- Constants ---------- */

#define RULE_MAX_ENTRIES         32   /**< Maximum rules per domain profile. */
#define RULE_ENGINE_MAX_ACTIONS  8    /**< Max actions per rule evaluation cycle. */

/* ---------- Rule classification ---------- */

/**
 * Rule criticality classes.
 * SAFETY_LOCKED rules are baked into the profile at build time
 * and cannot be modified via the field config portal.
 * OPERATIONAL rules are user-configurable in the field.
 */
typedef enum {
    RULE_CLASS_SAFETY_LOCKED  = 0,   /**< Immutable safety rules — read-only from field. */
    RULE_CLASS_OPERATIONAL    = 1    /**< Field-configurable operational rules. */
} RuleClass_t;

/**
 * Rule action types.
 */
typedef enum {
    RULE_ACTION_NONE          = 0,   /**< No action (monitoring-only rule). */
    RULE_ACTION_SET_ACTUATOR  = 1,   /**< Set an actuator to a commanded state. */
    RULE_ACTION_TRIGGER_ALERT = 2,   /**< Raise an alert/alarm. */
    RULE_ACTION_INTERLOCK     = 3    /**< Trigger an interlock sequence. */
} RuleActionType_t;

/**
 * Actuator state commanded by a rule.
 */
typedef enum {
    ACTUATOR_STATE_OFF    = 0,
    ACTUATOR_STATE_ON     = 1,
    ACTUATOR_STATE_SAFE   = 2   /**< Go to defined safe position. */
} ActuatorState_t;

/**
 * Rule entry — exactly 16 bytes packed.
 *
 * Byte layout:
 *   [0-1]  rule_id       (uint16_t, 2 bytes)
 *   [2]    priority       (uint8_t,  1 byte) — 0=highest
 *   [3-6]  threshold      (float,    4 bytes)
 *   [7]    action_type    (uint8_t,  1 byte) — RuleActionType_t
 *   [8]    actuator_id    (uint8_t,  1 byte)
 *   [9]    metric_id      (uint8_t,  1 byte)
 *   [10]   rule_class     (uint8_t,  1 byte) — RuleClass_t
 *   [11]   interlock_id   (uint8_t,  1 byte) — 0 = no interlock
 *   [12]    comparison_type (uint8_t, 1 byte) — 0=ABOVE (>=), 1=BELOW (<)
 *   [13-15] reserved        (uint8_t[3], 3 bytes)
 *
 * Total: 13 bytes data + 3 bytes reserved = 16 bytes.
 *
 * NOTE: comparison_type was added in Phase 2 to support Agriculture's
 * pump dry-run protection rule (trigger when value < threshold). The
 * Home/Building profile does not use this field (defaults to ABOVE).
 *
 * RULE_COMPARE_OUTSIDE_RANGE was added in Phase 6.1 and removed in
 * Phase 6.2 — the low-pH safety fix ended up using two separate
 * RULE_COMPARE_BELOW/ABOVE rules instead, leaving the OUTSIDE_RANGE
 * branch untested in the safety-critical rule engine core. Per the
 * Phase 2 precedent (every core change justified by actual immediate
 * need), the unused mechanism was removed rather than shipped as
 * speculative dead code.
 */
typedef enum {
    RULE_COMPARE_ABOVE = 0,  /**< Trigger when value >= threshold. */
    RULE_COMPARE_BELOW = 1   /**< Trigger when value < threshold. */
} RuleComparisonType_t;

typedef struct {
    uint16_t rule_id;          /* bytes 0-1  */
    uint8_t  priority;         /* byte 2     */
    float    threshold;        /* bytes 3-6  */
    uint8_t  action_type;      /* byte 7     */
    uint8_t  actuator_id;      /* byte 8     */
    uint8_t  metric_id;        /* byte 9     */
    uint8_t  rule_class;       /* byte 10    */
    uint8_t  interlock_id;     /* byte 11    */
    uint8_t  comparison_type;  /* byte 12    */
    uint8_t  reserved[3];      /* bytes 13-15 */
} __attribute__((packed)) RuleEntry_t;

/* Compile-time assertion: RuleEntry_t must be exactly 16 bytes. */
_Static_assert(sizeof(RuleEntry_t) == 16, "RuleEntry_t must be 16 bytes packed");

/* ---------- Evaluation result ---------- */

/**
 * Result of evaluating a single rule.
 */
typedef struct {
    uint16_t rule_id;           /**< Which rule was evaluated. */
    bool     triggered;         /**< true if the rule condition was met. */
    uint8_t  action_type;       /**< Action to take (RuleActionType_t). */
    uint8_t  actuator_id;       /**< Target actuator (if action is SET_ACTUATOR). */
    uint8_t  actuator_state;    /**< Commanded state (ActuatorState_t). */
} RuleEvaluationResult_t;

/* ---------- Sensor reading (passed to rule engine) ---------- */

/**
 * Sensor reading used by the rule engine for evaluation.
 * Must pass sensor validation gate (Section 9) before reaching here.
 */
typedef struct {
    uint8_t  node_id;
    uint8_t  metric_id;
    float    value;
    uint64_t timestamp_ms;
} SensorReading_t;

/* ---------- Domain Profile VTable ---------- */

/**
 * Forward declarations for vtable function signatures.
 */
typedef struct RuleEntry RuleEntryPlaceholder; /* avoid circular include */

/**
 * VTable interface that a domain profile must implement.
 * The rule engine core calls through this table — it contains
 * zero domain-specific logic.
 *
 * Architecture ref: Section 6, Interface Pattern (vtable-based).
 */
typedef struct {
    /**
     * Return a pointer to the domain's rule table and its size.
     * @param out_entries  Output: pointer to the rule array.
     * @param out_count    Output: number of rules in the table.
     */
    void (*getRuleTable)(const RuleEntry_t **out_entries, uint8_t *out_count);

    /**
     * Validate a sensor reading against domain-specific plausibility bounds.
     * @param reading      The sensor reading to validate.
     * @return true if the reading is within acceptable bounds.
     */
    bool (*validateSensorReading)(const SensorReading_t *reading);

    /**
     * Return the fail-safe mode for a given actuator.
     * @param actuator_id   Which actuator.
     * @param is_power_loss  true if this is a power-loss event, false for comms-loss.
     * @return The fail-safe action to take (ActuatorState_t value).
     */
    uint8_t (*getFailSafeMode)(uint8_t actuator_id, bool is_power_loss);

    /**
     * Execute an action on an actuator.
     * @param actuator_id   Which actuator.
     * @param state         Commanded state.
     * @return true if the action was executed successfully.
     */
    bool (*executeAction)(uint8_t actuator_id, uint8_t state);
} DomainProfileVTable_t;

/* ---------- Rule Engine Core API ---------- */

/**
 * Initialise the rule engine core.
 *
 * @return true on success.
 */
bool rule_engine_init(void);

/**
 * Evaluate all rules against a sensor reading, using the provided vtable.
 *
 * @param vtable      Domain profile vtable.
 * @param reading     The sensor reading (must have passed validation).
 * @param results     Output array for evaluation results.
 * @param max_results Maximum number of results the output array can hold.
 * @return Number of rules that were triggered.
 */
uint8_t rule_engine_evaluate(
    const DomainProfileVTable_t *vtable,
    const SensorReading_t *reading,
    RuleEvaluationResult_t *results,
    uint8_t max_results
);

/**
 * Find a rule by its ID.
 *
 * @param vtable    Domain profile vtable.
 * @param rule_id   The rule ID to find.
 * @return Pointer to the rule entry, or NULL if not found.
 */
const RuleEntry_t *rule_engine_find_rule(
    const DomainProfileVTable_t *vtable,
    uint16_t rule_id
);

/**
 * Check whether a rule is safety-locked (immutable from field config).
 *
 * @param rule  The rule to check.
 * @return true if the rule is RULE_CLASS_SAFETY_LOCKED.
 */
static inline bool rule_is_safety_locked(const RuleEntry_t *rule)
{
    return (rule != NULL && rule->rule_class == (uint8_t)RULE_CLASS_SAFETY_LOCKED);
}

#endif /* RULE_ENGINE_CORE_H */
