/**
 * @file ingestion_handler.c
 * @brief Hub-side ingestion handler implementation.
 *
 * Architecture ref: Section 2 (Tier 3 — Hub), Section 9 (validation gate).
 *
 * This is the production code path that closes the "zero production callers"
 * gap. Every security gate built across Phases 13-15.2 is now exercised
 * by real (non-test) code:
 *
 *   radio_receive() → gateway_ingest_frame(tracker, edge_tracker) →
 *     for each entry: vtable->validateSensorReading() → rule_engine_evaluate()
 *
 * Domain-agnostic: configured via IngestionHandlerConfig_t function pointers.
 * The same handler compiles for Home, Agriculture, HVAC, and Water Treatment
 * profiles — only the configuration changes.
 */

#include "ingestion_handler.h"
#include "../gateway/batch_forwarder.h"  /* for BatchFrameHeader_t, BatchFrameEntry_t */
#include "../shared/audit_log.h"
#include "../shared/rule_engine_core.h"
#include "../shared/sensor_validation.h"
#include "historian.h"  /* Phase 16: offline-resilience storage */
#include <string.h>

/* ---------- Module state ---------- */

static IngestionHandlerConfig_t  s_config;
static IngestionHandlerStats_t   s_stats;
static bool                      s_initialised = false;

/* ---------- API ---------- */

bool ingestion_handler_init(const IngestionHandlerConfig_t *config)
{
    if (!config) return false;
    if (!config->gw_tracker) return false;
    if (!config->edge_tracker) return false;
    if (!config->vtable) return false;
    if (!config->radio_receive) return false;

    /* bounds_lookup and history_lookup may be NULL — some metrics
     * (e.g. binary/state metrics like HOME_METRIC_SECURITY_STATE)
     * have no standalone SensorValidationBounds_t. The vtable's
     * validateSensorReading() is the fallback validation gate. */

    memcpy(&s_config, config, sizeof(s_config));
    memset(&s_stats, 0, sizeof(s_stats));
    s_initialised = true;

    return true;
}

uint8_t ingestion_handler_poll(uint64_t current_ms)
{
    if (!s_initialised) return 0;

    s_stats.poll_count++;

    /* Step 1: Poll radio for incoming frame. */
    uint8_t radio_payload[256];
    size_t  radio_len = 0;

    if (!s_config.radio_receive(radio_payload, sizeof(radio_payload), &radio_len)) {
        return 0;  /* No data available — non-blocking return. */
    }

    s_stats.frames_received++;

    /* Step 2: Ingest via gateway_ingest_frame (verify + decode + trust filter). */
    BatchFrameHeader_t header;
    BatchFrameEntry_t entries[INGESTION_HANDLER_MAX_ENTRIES];
    uint8_t entry_count = 0;

    GatewayFrameResult_t gr = gateway_ingest_frame(
        s_config.gw_tracker, s_config.edge_tracker,
        radio_payload, radio_len,
        &header, entries, INGESTION_HANDLER_MAX_ENTRIES, &entry_count);

    if (gr != GW_FRAME_OK) {
        s_stats.frames_rejected++;
        return 0;
    }

    s_stats.frames_ingested++;

    /* Step 3: For each accepted entry — validate and evaluate rules. */
    uint8_t total_triggered = 0;

    for (uint8_t i = 0; i < entry_count; i++) {
        s_stats.entries_processed++;

        /* Build SensorReading from batch entry.
         * Interface note: BatchFrameEntry_t.node_id is uint16_t,
         * SensorReading_t.node_id is uint8_t. Safe because Edge
         * node IDs are always <= 255 (uint8 origin). */
        SensorReading_t reading;
        reading.node_id      = (uint8_t)entries[i].node_id;
        reading.metric_id    = entries[i].metric_id;
        reading.value        = entries[i].value;
        reading.timestamp_ms = current_ms;

        /* Gate 1: Vtable sensor validation (domain-specific). */
        if (!s_config.vtable->validateSensorReading(&reading)) {
            s_stats.entries_rejected++;
            audit_log_add(AUDIT_AUTH_FAILURE, reading.node_id, 0,
                          "Ingestion handler: vtable validation rejected entry");
            continue;
        }

        /* Gate 2: Standalone sensorValidate() with domain bounds (if available).
         * Some metrics (binary/state) have no bounds — vtable validation is
         * the sole gate for those. This is the Phase 15 INTERFACE FINDING. */
        if (s_config.bounds_lookup && s_config.history_lookup) {
            const SensorValidationBounds_t *bounds = s_config.bounds_lookup(reading.metric_id);
            if (bounds != NULL) {
                SensorHistory_t *hist = s_config.history_lookup(reading.metric_id);
                if (hist != NULL) {
                    SensorValidationResult_t svr = sensorValidate(
                        reading.node_id, reading.metric_id, reading.value,
                        bounds, hist, current_ms);
                    if (svr != SENSOR_VALID) {
                        s_stats.entries_rejected++;
                        audit_log_add(AUDIT_AUTH_FAILURE, reading.node_id, 0,
                                      "Ingestion handler: sensorValidate rejected entry");
                        continue;
                    }
                    sensor_history_record(hist, reading.node_id, reading.metric_id,
                                          reading.value, current_ms);
                }
            }
        }

        s_stats.entries_valid++;  /* Passed both validation gates. */

        /* Phase 16: Write to historian (offline resilience).
         * Every entry that passes attestation + validation is recorded,
         * regardless of cloud connectivity. The historian encrypts at rest
         * and stores in a capacity-bounded ring buffer. */
        {
            HistorianRecord_t hrec;
            hrec.timestamp_ms      = current_ms;
            hrec.value             = reading.value;
            hrec.sequence          = 0;  /* Updated by historian internally */
            hrec.node_id           = reading.node_id;
            hrec.metric_id         = reading.metric_id;
            hrec.domain_profile_id = s_config.domain_profile_id;
            memset(hrec.reserved, 0, sizeof(hrec.reserved));
            historian_write(&hrec);
        }

        /* Gate 3: Rule engine evaluation. */
        RuleEvaluationResult_t results[RULE_ENGINE_MAX_ACTIONS];
        uint8_t triggered = s_config.vtable->getRuleTable
            ? rule_engine_evaluate(s_config.vtable, &reading,
                                   results, RULE_ENGINE_MAX_ACTIONS)
            : 0;

        total_triggered += triggered;
        s_stats.rules_triggered += triggered;
    }

    return total_triggered;
}

const IngestionHandlerStats_t *ingestion_handler_get_stats(void)
{
    return &s_stats;
}

void ingestion_handler_reset_stats(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
}
