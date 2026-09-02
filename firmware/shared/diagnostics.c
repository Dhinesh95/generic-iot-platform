/**
 * @file diagnostics.c
 * @brief Diagnostics implementation — system health tracking.
 */

#include "diagnostics.h"
#include <string.h>
#include <stdio.h>

/* ---------- Counter name mapping ---------- */

typedef struct {
    const char *name;
    uint32_t   *ptr;
} CounterMapping_t;

static CounterMapping_t s_counter_map[] = {
    { "uptime_ms",              NULL }, /* Special: set via diagnostics_set_uptime */
    { "free_heap_bytes",        NULL },
    { "radio_tx_count",         NULL },
    { "radio_rx_count",         NULL },
    { "radio_errors",           NULL },
    { "attestation_success",    NULL },
    { "attestation_failures",   NULL },
    { "rule_engine_evaluations",NULL },
    { "rule_engine_safety_fires",NULL },
    { "historian_writes",       NULL },
    { "historian_evictions",    NULL },
    { "ota_transfers_attempted",NULL },
    { "ota_transfers_succeeded",NULL },
    { "ota_transfers_failed",   NULL },
    { "mqtt_publish_count",     NULL },
    { "mqtt_reconnect_count",   NULL },
    { "watchdog_resets",        NULL },
    { NULL, NULL }
};

static uint32_t *find_counter(DiagnosticsContext_t *ctx, const char *name)
{
    SystemDiagnostics_t *s = &ctx->snapshot;

    /* Map counter names to struct fields. */
    if (strcmp(name, "uptime_ms") == 0) return &s->uptime_ms;
    if (strcmp(name, "free_heap_bytes") == 0) return &s->free_heap_bytes;
    if (strcmp(name, "radio_tx_count") == 0) return &s->radio_tx_count;
    if (strcmp(name, "radio_rx_count") == 0) return &s->radio_rx_count;
    if (strcmp(name, "radio_errors") == 0) return &s->radio_errors;
    if (strcmp(name, "attestation_success") == 0) return &s->attestation_success;
    if (strcmp(name, "attestation_failures") == 0) return &s->attestation_failures;
    if (strcmp(name, "rule_engine_evaluations") == 0) return &s->rule_engine_evaluations;
    if (strcmp(name, "rule_engine_safety_fires") == 0) return &s->rule_engine_safety_fires;
    if (strcmp(name, "historian_writes") == 0) return &s->historian_writes;
    if (strcmp(name, "historian_evictions") == 0) return &s->historian_evictions;
    if (strcmp(name, "ota_transfers_attempted") == 0) return &s->ota_transfers_attempted;
    if (strcmp(name, "ota_transfers_succeeded") == 0) return &s->ota_transfers_succeeded;
    if (strcmp(name, "ota_transfers_failed") == 0) return &s->ota_transfers_failed;
    if (strcmp(name, "mqtt_publish_count") == 0) return &s->mqtt_publish_count;
    if (strcmp(name, "mqtt_reconnect_count") == 0) return &s->mqtt_reconnect_count;
    if (strcmp(name, "watchdog_resets") == 0) return &s->watchdog_resets;

    return NULL;
}

static uint8_t find_counter_index(const char *name)
{
    for (uint8_t i = 0; s_counter_map[i].name != NULL; i++) {
        if (strcmp(s_counter_map[i].name, name) == 0) return i;
    }
    return 0xFF;
}

/* ---------- API ---------- */

DiagResult_t diagnostics_init(DiagnosticsContext_t *ctx)
{
    if (!ctx) return DIAG_OP_ERR_PARAM_NULL;
    memset(ctx, 0, sizeof(*ctx));
    ctx->initialized = true;
    return DIAG_OP_OK;
}

DiagResult_t diagnostics_increment(DiagnosticsContext_t *ctx,
                                    const char *counter_name)
{
    return diagnostics_add(ctx, counter_name, 1);
}

DiagResult_t diagnostics_add(DiagnosticsContext_t *ctx,
                              const char *counter_name,
                              uint32_t amount)
{
    if (!ctx || !ctx->initialized || !counter_name) return DIAG_OP_ERR_PARAM_NULL;

    uint32_t *ptr = find_counter(ctx, counter_name);
    if (!ptr) return DIAG_OP_OK; /* Unknown counter — silently ignore. */

    uint32_t old_value = *ptr;
    *ptr += amount;

    /* Record event in history. */
    uint8_t idx = find_counter_index(counter_name);
    if (idx != 0xFF) {
        diagnostics_record_event(ctx, idx, old_value, *ptr);
    }

    return DIAG_OP_OK;
}

DiagResult_t diagnostics_get_snapshot(const DiagnosticsContext_t *ctx,
                                      SystemDiagnostics_t *snapshot)
{
    if (!ctx || !snapshot) return DIAG_OP_ERR_PARAM_NULL;
    memcpy(snapshot, &ctx->snapshot, sizeof(SystemDiagnostics_t));
    return DIAG_OP_OK;
}

DiagResult_t diagnostics_get_counter(const DiagnosticsContext_t *ctx,
                                      const char *counter_name,
                                      uint32_t *value)
{
    if (!ctx || !counter_name || !value) return DIAG_OP_ERR_PARAM_NULL;

    /* Cast away const for find_counter (it only reads, doesn't modify). */
    uint32_t *ptr = find_counter((DiagnosticsContext_t *)ctx, counter_name);
    if (!ptr) return DIAG_OP_ERR_PARAM_NULL;

    *value = *ptr;
    return DIAG_OP_OK;
}

DiagResult_t diagnostics_set_uptime(DiagnosticsContext_t *ctx, uint32_t ms)
{
    if (!ctx || !ctx->initialized) return DIAG_OP_ERR_PARAM_NULL;
    ctx->snapshot.uptime_ms = ms;
    return DIAG_OP_OK;
}

DiagResult_t diagnostics_record_event(DiagnosticsContext_t *ctx,
                                       uint8_t counter_index,
                                       uint32_t old_value,
                                       uint32_t new_value)
{
    if (!ctx || !ctx->initialized) return DIAG_OP_ERR_PARAM_NULL;

    uint16_t slot = (ctx->history_head + ctx->history_count) % DIAG_HISTORY_SIZE;
    if (ctx->history_count < DIAG_HISTORY_SIZE) {
        ctx->history_count++;
    } else {
        ctx->history_head = (ctx->history_head + 1) % DIAG_HISTORY_SIZE;
    }

    ctx->history[slot].counter_index = counter_index;
    ctx->history[slot].old_value = old_value;
    ctx->history[slot].new_value = new_value;

    return DIAG_OP_OK;
}

size_t diagnostics_to_json(const DiagnosticsContext_t *ctx,
                            char *buf, size_t buf_size)
{
    if (!ctx || !buf || buf_size == 0) return 0;

    const SystemDiagnostics_t *s = &ctx->snapshot;
    int written = snprintf(buf, buf_size,
        "{"
        "\"uptime_ms\":%lu,"
        "\"free_heap_bytes\":%lu,"
        "\"radio_tx\":%lu,"
        "\"radio_rx\":%lu,"
        "\"radio_errors\":%lu,"
        "\"attestation_ok\":%lu,"
        "\"attestation_fail\":%lu,"
        "\"rule_eval\":%lu,"
        "\"rule_safety\":%lu,"
        "\"historian_writes\":%lu,"
        "\"historian_evict\":%lu,"
        "\"ota_attempted\":%lu,"
        "\"ota_succeeded\":%lu,"
        "\"ota_failed\":%lu,"
        "\"mqtt_pub\":%lu,"
        "\"mqtt_reconn\":%lu,"
        "\"wdt_resets\":%lu,"
        "\"active_faults\":%lu,"
        "\"degradation\":%d"
        "}",
        (unsigned long)s->uptime_ms,
        (unsigned long)s->free_heap_bytes,
        (unsigned long)s->radio_tx_count,
        (unsigned long)s->radio_rx_count,
        (unsigned long)s->radio_errors,
        (unsigned long)s->attestation_success,
        (unsigned long)s->attestation_failures,
        (unsigned long)s->rule_engine_evaluations,
        (unsigned long)s->rule_engine_safety_fires,
        (unsigned long)s->historian_writes,
        (unsigned long)s->historian_evictions,
        (unsigned long)s->ota_transfers_attempted,
        (unsigned long)s->ota_transfers_succeeded,
        (unsigned long)s->ota_transfers_failed,
        (unsigned long)s->mqtt_publish_count,
        (unsigned long)s->mqtt_reconnect_count,
        (unsigned long)s->watchdog_resets,
        (unsigned long)s->active_fault_count,
        (int)s->current_degradation
    );

    return (written > 0 && (size_t)written < buf_size) ? (size_t)written : 0;
}

void diagnostics_print_summary(const DiagnosticsContext_t *ctx)
{
    if (!ctx || !ctx->initialized) return;

    const SystemDiagnostics_t *s = &ctx->snapshot;
    printf("=== System Diagnostics ===\n");
    printf("  Uptime:             %lu ms\n", (unsigned long)s->uptime_ms);
    printf("  Free heap:          %lu bytes\n", (unsigned long)s->free_heap_bytes);
    printf("  Radio TX/RX/Err:    %lu / %lu / %lu\n",
           (unsigned long)s->radio_tx_count,
           (unsigned long)s->radio_rx_count,
           (unsigned long)s->radio_errors);
    printf("  Attestation OK/Fail:%lu / %lu\n",
           (unsigned long)s->attestation_success,
           (unsigned long)s->attestation_failures);
    printf("  Rule eval/safety:   %lu / %lu\n",
           (unsigned long)s->rule_engine_evaluations,
           (unsigned long)s->rule_engine_safety_fires);
    printf("  Historian W/E:      %lu / %lu\n",
           (unsigned long)s->historian_writes,
           (unsigned long)s->historian_evictions);
    printf("  Active faults:      %lu\n", (unsigned long)s->active_fault_count);
    printf("  Degradation level:  %d\n", (int)s->current_degradation);
    printf("==========================\n");
}
