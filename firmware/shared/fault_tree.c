/**
 * @file fault_tree.c
 * @brief Fault tree implementation — graceful degradation matrix.
 */

#include "fault_tree.h"
#include <string.h>

/**
 * Degradation matrix: fault_id × current_level → new_level.
 * Each fault can only move the system UP (worse), never down (better).
 * Recovery happens only when ALL faults are cleared.
 */
static const DegradationLevel_t s_degradation_matrix[FAULT_COUNT][5] = {
    /*                           L0→   L1→   L2→   L3→   L4→  */
    [FAULT_NONE]              = {  0,    0,    0,    0,    0 },
    [FAULT_RADIO_LOST]        = {  1,    2,    2,    3,    4 },
    [FAULT_CLOUD_UNREACHABLE] = {  1,    2,    2,    3,    4 },
    [FAULT_SENSOR_INVALID]    = {  1,    2,    3,    3,    4 },
    [FAULT_ATTESTATION_FAILED]= {  2,    3,    3,    3,    4 },
    [FAULT_FLASH_WRITE_ERROR] = {  1,    1,    2,    3,    4 },
    [FAULT_CLOCK_DRIFT]       = {  1,    2,    2,    3,    4 },
    [FAULT_MEMORY_LOW]        = {  1,    2,    3,    4,    4 },
    [FAULT_RULE_ENGINE_STALL] = {  3,    4,    4,    4,    4 },
};

static DegradationLevel_t evaluate_system_level(const FaultTreeContext_t *ctx)
{
    DegradationLevel_t max_level = DEGRADE_LEVEL_NORMAL;

    for (uint8_t i = 0; i < ctx->fault_count; i++) {
        if (ctx->faults[i].active) {
            if (ctx->faults[i].current_level > max_level) {
                max_level = ctx->faults[i].current_level;
            }
        }
    }

    return max_level;
}

/* ---------- API ---------- */

bool fault_tree_init(FaultTreeContext_t *ctx)
{
    if (!ctx) return false;
    memset(ctx, 0, sizeof(*ctx));
    ctx->system_level = DEGRADE_LEVEL_NORMAL;
    ctx->initialized = true;
    return true;
}

DegradationLevel_t fault_tree_report_fault(FaultTreeContext_t *ctx,
                                            FaultId_t fault_id,
                                            uint32_t timestamp)
{
    if (!ctx || !ctx->initialized || fault_id >= FAULT_COUNT) {
        return DEGRADE_LEVEL_EMERGENCY;
    }

    /* Find existing fault slot or allocate new one. */
    int8_t slot = -1;
    for (uint8_t i = 0; i < ctx->fault_count; i++) {
        if (ctx->faults[i].fault_id == fault_id) {
            slot = (int8_t)i;
            break;
        }
    }

    if (slot < 0 && ctx->fault_count < FAULT_TREE_MAX_FAULTS) {
        slot = (int8_t)ctx->fault_count++;
    }

    if (slot < 0) return ctx->system_level; /* No room. */

    FaultState_t *f = &ctx->faults[slot];
    f->fault_id = fault_id;
    f->active = true;
    f->count++;

    /* Apply degradation matrix: take the worse of current and matrix result. */
    DegradationLevel_t new_level = s_degradation_matrix[fault_id][ctx->system_level];
    if (new_level > f->current_level) {
        f->current_level = new_level;
    }
    f->since_ms = timestamp;

    /* Recalculate system level. */
    ctx->system_level = evaluate_system_level(ctx);

    return ctx->system_level;
}

DegradationLevel_t fault_tree_clear_fault(FaultTreeContext_t *ctx,
                                           FaultId_t fault_id)
{
    if (!ctx || !ctx->initialized || fault_id >= FAULT_COUNT) {
        return DEGRADE_LEVEL_EMERGENCY;
    }

    for (uint8_t i = 0; i < ctx->fault_count; i++) {
        if (ctx->faults[i].fault_id == fault_id) {
            ctx->faults[i].active = false;
            ctx->faults[i].current_level = DEGRADE_LEVEL_NORMAL;
            break;
        }
    }

    /* Recalculate system level. */
    ctx->system_level = evaluate_system_level(ctx);

    return ctx->system_level;
}

DegradationLevel_t fault_tree_get_level(const FaultTreeContext_t *ctx)
{
    if (!ctx || !ctx->initialized) return DEGRADE_LEVEL_EMERGENCY;
    return ctx->system_level;
}

bool fault_tree_is_fault_active(const FaultTreeContext_t *ctx, FaultId_t fault_id)
{
    if (!ctx || !ctx->initialized || fault_id >= FAULT_COUNT) return false;

    for (uint8_t i = 0; i < ctx->fault_count; i++) {
        if (ctx->faults[i].fault_id == fault_id && ctx->faults[i].active) {
            return true;
        }
    }
    return false;
}

uint8_t fault_tree_get_active_count(const FaultTreeContext_t *ctx)
{
    if (!ctx || !ctx->initialized) return 0;

    uint8_t count = 0;
    for (uint8_t i = 0; i < ctx->fault_count; i++) {
        if (ctx->faults[i].active) count++;
    }
    return count;
}
