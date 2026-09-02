/**
 * @file edge_node.c
 * @brief Edge Node main loop implementation.
 *
 * Architecture ref: Section 8, Tier 1.
 *
 * Reuses firmware/shared/rs485_protocol.c for frame encoding —
 * the same frame format Gateway (Phase 13) already parses.
 */

#include "edge_node.h"
#include "sensor_driver.h"
#include "edge_config.h"
#include "edge_attestation.h"
#include "../shared/rs485_protocol.h"
#include <string.h>

/* ---------- Module state ---------- */

static RS485SendFunc_t    s_send_fn = NULL;
static EdgeAuthContext_t  *s_auth_ctx = NULL;
static bool               s_auth_disabled_test_only = false;
static EdgeNodeStats_t    s_stats;

/* ---------- API ---------- */

void edge_node_init(RS485SendFunc_t send_fn, EdgeAuthContext_t *auth_ctx)
{
    s_send_fn = send_fn;
    s_auth_disabled_test_only = false;

    /* NULL is always a parameter error — fail closed. */
    if (auth_ctx == NULL) {
        s_auth_ctx = NULL;
        /* edge_node_read_and_send will reject: NULL auth = no auth = no data. */
    } else if (auth_ctx == EDGE_AUTH_UNGATED_TESTING_ONLY) {
        /* Explicit test-only bypass — auth check is skipped entirely.
         * This is the ONLY way to disable auth gating, and the sentinel
         * name makes the security bypass explicit at the call site. */
        s_auth_ctx = NULL;
        s_auth_disabled_test_only = true;
    } else {
        s_auth_ctx = auth_ctx;
    }
    memset(&s_stats, 0, sizeof(s_stats));
}

EdgeNodeResult_t edge_node_read_and_send(uint64_t current_ms)
{
    (void)current_ms;

    EdgeNodeResult_t result;
    memset(&result, 0, sizeof(result));
    result.valid = false;
    result.frame_sent = false;

    s_stats.reads_total++;

    /* 0. Attestation gate (same pattern as Phase 6.6 LoRa join).
     * No sensor data is sent before the Edge Node has completed
     * attestation with the Hub. This prevents unauthenticated nodes
     * from injecting data into the system.
     *
     * NULL auth_ctx → reject (fail-closed, no auth = no data).
     * EDGE_AUTH_UNGATED_TESTING_ONLY → skip check (test-only).
     * Valid auth_ctx → check edge_auth_is_authenticated(). */
    if (s_auth_disabled_test_only) {
        /* Explicit test-only bypass — no auth check. */
    } else if (s_auth_ctx == NULL) {
        /* NULL auth_ctx: no authentication provisioned → reject. */
        s_stats.reads_invalid++;
        return result;
    } else if (!edge_auth_is_authenticated(s_auth_ctx)) {
        /* Auth context exists but not yet authenticated → reject. */
        s_stats.reads_invalid++;
        return result;
    }

    /* 1. Check if node is provisioned. */
    const EdgeNodeConfig_t *cfg = edge_config_get();
    if (!cfg) {
        s_stats.reads_invalid++;
        return result;
    }

    result.node_id = cfg->node_id;
    result.gw_addr = cfg->gw_dest_addr;

    /* 2. Look up the active driver. */
    const SensorDriver_t *driver = sensor_driver_find(cfg->active_driver);
    if (!driver) {
        s_stats.reads_invalid++;
        return result;
    }

    /* 3. Read from the sensor. */
    SensorDriverReading_t reading = driver->read();
    if (!reading.valid) {
        s_stats.reads_invalid++;
        return result;
    }

    /* 4. Validate via driver-level check. */
    if (!driver->validate(reading.value)) {
        s_stats.reads_invalid++;
        return result;
    }

    /* 5. Apply scale factor and offset from config. */
    float physical_value = reading.value * cfg->driver_config.scale_factor
                         + cfg->driver_config.offset;

    result.value = physical_value;
    result.valid = true;
    s_stats.reads_valid++;

    /* 6. Encode into RS-485 frame and send.
     *
     * Payload format (simple, domain-agnostic):
     *   [node_id(1)] [metric_id(1)] [value_bytes(4, IEEE 754 LE)]
     *
     * The Gateway (Phase 13) parses this via rs485_parse() and
     * feeds it into the stateful cache / delta filter pipeline.
     */
    uint8_t payload[6];
    payload[0] = cfg->node_id;
    payload[1] = cfg->metric_id;  /* Semantic metric ID for Hub's rule engine */
    /* Encode float as 4 little-endian bytes. */
    float val = physical_value;
    memcpy(&payload[2], &val, 4);

    RS485Frame_t frame;
    rs485_create_frame(&frame, cfg->gw_dest_addr, cfg->node_id,
                       payload, sizeof(payload));

    /* Encode to wire format. */
    uint8_t wire_buf[RS485_MAX_FRAME_PAYLOAD + 8]; /* header + payload + CRC */
    size_t wire_len = 0;

    if (!rs485_encode(&frame, wire_buf, sizeof(wire_buf), &wire_len)) {
        s_stats.frames_failed++;
        return result;
    }

    /* Send via the transport callback. */
    if (s_send_fn && s_send_fn(wire_buf, wire_len)) {
        result.frame_sent = true;
        s_stats.frames_sent++;
    } else {
        s_stats.frames_failed++;
    }

    return result;
}

const EdgeNodeStats_t *edge_node_get_stats(void)
{
    return &s_stats;
}

void edge_node_reset_stats(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
}
