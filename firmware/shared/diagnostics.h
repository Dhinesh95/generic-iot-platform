/**
 * @file diagnostics.h
 * @brief Field diagnostics & observability — system health tracking.
 *
 * Architecture ref: Phase 22 (Field Diagnostics & Observability).
 *
 * Tracks system health counters, active faults, degradation state,
 * and provides structured JSON output for the config portal and cloud.
 */

#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "fault_tree.h"

/* ---------- Constants ---------- */

#define DIAG_JSON_MAX_SIZE           2048
#define DIAG_HISTORY_SIZE            64
#define DIAG_COUNTER_NAME_MAX_LEN    32

/* ---------- Types ---------- */

typedef enum {
    DIAG_OP_OK,
    DIAG_OP_ERR_NOT_INIT,
    DIAG_OP_ERR_PARAM_NULL,
    DIAG_OP_ERR_BUFFER_TOO_SMALL
} DiagResult_t;

/**
 * System diagnostics counters (snapshot).
 */
typedef struct {
    uint32_t uptime_ms;
    uint32_t free_heap_bytes;
    uint32_t radio_tx_count;
    uint32_t radio_rx_count;
    uint32_t radio_errors;
    uint32_t attestation_success;
    uint32_t attestation_failures;
    uint32_t rule_engine_evaluations;
    uint32_t rule_engine_safety_fires;
    uint32_t historian_writes;
    uint32_t historian_evictions;
    uint32_t ota_transfers_attempted;
    uint32_t ota_transfers_succeeded;
    uint32_t ota_transfers_failed;
    uint32_t mqtt_publish_count;
    uint32_t mqtt_reconnect_count;
    uint32_t watchdog_resets;
    uint32_t active_fault_count;
    DegradationLevel_t current_degradation;
} SystemDiagnostics_t;

/**
 * Diagnostic state transition (for history ring buffer).
 */
typedef struct {
    uint32_t           timestamp_ms;
    uint32_t           old_value;
    uint32_t           new_value;
    uint8_t            counter_index;  /**< Which counter changed. */
} DiagEvent_t;

/**
 * Diagnostics context (all state in one struct for testability).
 */
typedef struct {
    SystemDiagnostics_t snapshot;
    DiagEvent_t         history[DIAG_HISTORY_SIZE];
    uint16_t            history_head;
    uint16_t            history_count;
    bool                initialized;
} DiagnosticsContext_t;

/* ---------- API ---------- */

/**
 * Initialize the diagnostics subsystem.
 *
 * @param ctx  Diagnostics context (caller-owned).
 * @return DIAG_OP_OK on success.
 */
DiagResult_t diagnostics_init(DiagnosticsContext_t *ctx);

/**
 * Increment a named counter. If the counter name is not recognized,
 * the call is silently ignored (counters are additive-only by design).
 *
 * @param ctx          Diagnostics context.
 * @param counter_name Name of the counter to increment.
 * @return DIAG_OP_OK on success.
 */
DiagResult_t diagnostics_increment(DiagnosticsContext_t *ctx,
                                    const char *counter_name);

/**
 * Increment a counter by a specific amount.
 *
 * @param ctx          Diagnostics context.
 * @param counter_name Name of the counter.
 * @param amount       Amount to add.
 * @return DIAG_OP_OK on success.
 */
DiagResult_t diagnostics_add(DiagnosticsContext_t *ctx,
                              const char *counter_name,
                              uint32_t amount);

/**
 * Get the current snapshot of all diagnostics.
 *
 * @param ctx       Diagnostics context.
 * @param snapshot  Output: current diagnostics snapshot.
 * @return DIAG_OP_OK on success.
 */
DiagResult_t diagnostics_get_snapshot(const DiagnosticsContext_t *ctx,
                                      SystemDiagnostics_t *snapshot);

/**
 * Get the value of a specific counter.
 *
 * @param ctx           Diagnostics context.
 * @param counter_name  Counter name.
 * @param value         Output: counter value.
 * @return DIAG_OP_OK on success, DIAG_OP_ERR_PARAM_NULL if counter not found.
 */
DiagResult_t diagnostics_get_counter(const DiagnosticsContext_t *ctx,
                                      const char *counter_name,
                                      uint32_t *value);

/**
 * Set the uptime (called periodically by the system tick).
 *
 * @param ctx  Diagnostics context.
 * @param ms   Current uptime in milliseconds.
 * @return DIAG_OP_OK on success.
 */
DiagResult_t diagnostics_set_uptime(DiagnosticsContext_t *ctx, uint32_t ms);

/**
 * Record a state transition in the history ring buffer.
 *
 * @param ctx            Diagnostics context.
 * @param counter_index  Which counter changed (index into snapshot).
 * @param old_value      Previous value.
 * @param new_value      New value.
 * @return DIAG_OP_OK on success.
 */
DiagResult_t diagnostics_record_event(DiagnosticsContext_t *ctx,
                                       uint8_t counter_index,
                                       uint32_t old_value,
                                       uint32_t new_value);

/**
 * Export diagnostics as JSON string.
 *
 * @param ctx      Diagnostics context.
 * @param buf      Output buffer.
 * @param buf_size Size of output buffer.
 * @return Number of bytes written (excluding null terminator), or 0 on error.
 */
size_t diagnostics_to_json(const DiagnosticsContext_t *ctx,
                            char *buf, size_t buf_size);

/**
 * Print a human-readable summary to stdout (for serial console).
 *
 * @param ctx  Diagnostics context.
 */
void diagnostics_print_summary(const DiagnosticsContext_t *ctx);

#endif /* DIAGNOSTICS_H */
