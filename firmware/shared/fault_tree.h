/**
 * @file fault_tree.h
 * @brief Fault tree — graceful degradation with defined fault paths.
 *
 * Architecture ref: Phase 20 (Graceful Degradation / Fault Tree).
 *
 * Every failure mode has a defined degradation response. The fault tree
 * maps (fault_id × current_level) → next_level, ensuring the system
 * always degrades gracefully rather than failing dangerously.
 *
 * Degradation levels:
 *   L0_NORMAL     — Full operation
 *   L1_REDUCED    — Reduced sampling rate, non-essential features off
 *   L2_LOCAL_ONLY — Cloud sync stopped, local operation only
 *   L3_SAFE_MODE  — Only safety rules active, all else off
 *   L4_EMERGENCY  — All actuators → safe state, system halted
 */

#ifndef FAULT_TREE_H
#define FAULT_TREE_H

#include <stdint.h>
#include <stdbool.h>

/* ---------- Constants ---------- */

#define FAULT_TREE_MAX_FAULTS   16

/* ---------- Types ---------- */

typedef enum {
    FAULT_NONE = 0,
    FAULT_RADIO_LOST,
    FAULT_CLOUD_UNREACHABLE,
    FAULT_SENSOR_INVALID,
    FAULT_ATTESTATION_FAILED,
    FAULT_FLASH_WRITE_ERROR,
    FAULT_CLOCK_DRIFT,
    FAULT_MEMORY_LOW,
    FAULT_RULE_ENGINE_STALL,
    FAULT_COUNT
} FaultId_t;

typedef enum {
    DEGRADE_LEVEL_NORMAL = 0,    /* L0: Full operation */
    DEGRADE_LEVEL_REDUCED,       /* L1: Reduced rate */
    DEGRADE_LEVEL_LOCAL_ONLY,    /* L2: No cloud */
    DEGRADE_LEVEL_SAFE_MODE,     /* L3: Safety only */
    DEGRADE_LEVEL_EMERGENCY      /* L4: All safe */
} DegradationLevel_t;

typedef struct {
    FaultId_t          fault_id;
    DegradationLevel_t current_level;
    uint32_t           since_ms;
    uint32_t           count;
    bool               active;
} FaultState_t;

typedef struct {
    FaultState_t      faults[FAULT_TREE_MAX_FAULTS];
    uint8_t           fault_count;
    DegradationLevel_t system_level;
    bool              initialized;
} FaultTreeContext_t;

/* ---------- API ---------- */

/**
 * Initialize the fault tree.
 *
 * @param ctx  Fault tree context (caller-owned).
 * @return true on success.
 */
bool fault_tree_init(FaultTreeContext_t *ctx);

/**
 * Report a fault occurrence. The fault tree evaluates the degradation
 * matrix and updates the system level.
 *
 * @param ctx        Fault tree context.
 * @param fault_id   Which fault occurred.
 * @param timestamp  Current timestamp in ms.
 * @return New system degradation level after this fault.
 */
DegradationLevel_t fault_tree_report_fault(FaultTreeContext_t *ctx,
                                            FaultId_t fault_id,
                                            uint32_t timestamp);

/**
 * Clear a fault (e.g. radio reconnected). May cause degradation to
 * improve if this was the only active fault.
 *
 * @param ctx        Fault tree context.
 * @param fault_id   Which fault to clear.
 * @return New system degradation level.
 */
DegradationLevel_t fault_tree_clear_fault(FaultTreeContext_t *ctx,
                                           FaultId_t fault_id);

/**
 * Get the current system degradation level.
 *
 * @param ctx  Fault tree context.
 * @return Current degradation level.
 */
DegradationLevel_t fault_tree_get_level(const FaultTreeContext_t *ctx);

/**
 * Check if a specific fault is currently active.
 *
 * @param ctx       Fault tree context.
 * @param fault_id  Which fault to check.
 * @return true if the fault is active.
 */
bool fault_tree_is_fault_active(const FaultTreeContext_t *ctx, FaultId_t fault_id);

/**
 * Get the number of active faults.
 *
 * @param ctx  Fault tree context.
 * @return Number of currently active faults.
 */
uint8_t fault_tree_get_active_count(const FaultTreeContext_t *ctx);

#endif /* FAULT_TREE_H */
