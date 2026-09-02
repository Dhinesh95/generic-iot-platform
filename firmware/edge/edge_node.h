/**
 * @file edge_node.h
 * @brief Edge Node main loop — read, validate, encode, send.
 *
 * Architecture ref: Section 8, Tier 1.
 *
 * The Edge Node loop:
 *   1. Read active sensor driver
 *   2. Validate raw reading via driver's validate()
 *   3. Encode into RS-485 frame using firmware/shared/rs485_protocol.c
 *   4. Send to Gateway (via mock RS-485 transport in tests,
 *      real UART in production)
 *
 * This reuses the exact RS-485 frame format that Gateway (Phase 13)
 * already parses — no protocol drift between the two sides.
 */

#ifndef EDGE_NODE_H
#define EDGE_NODE_H

#include <stdint.h>
#include <stdbool.h>
#include "edge_config.h"
#include "sensor_driver.h"
#include "edge_attestation.h"

/* ---------- Types ---------- */

/**
 * Result of a single edge-node read cycle.
 */
typedef struct {
    float    value;         /**< Sensor reading (physical units). */
    bool     valid;         /**< false if driver or validation failed. */
    uint8_t  node_id;       /**< Source node address. */
    uint8_t  gw_addr;       /**< Destination Gateway address. */
    bool     frame_sent;    /**< true if RS-485 frame was sent. */
} EdgeNodeResult_t;

/**
 * RS-485 send callback — abstracts the physical transport.
 * In tests: mock function that captures the wire bytes.
 * In production: real UART RS-485 driver.
 *
 * @param data      Wire bytes to send.
 * @param len       Number of bytes.
 * @return true on successful send.
 */
typedef bool (*RS485SendFunc_t)(const uint8_t *data, size_t len);

/**
 * Edge Node statistics.
 */
typedef struct {
    uint32_t  reads_total;       /**< Total read attempts. */
    uint32_t  reads_valid;       /**< Successful valid reads. */
    uint32_t  reads_invalid;     /**< Failed validation / driver fault. */
    uint32_t  frames_sent;       /**< RS-485 frames successfully sent. */
    uint32_t  frames_failed;     /**< RS-485 send failures. */
} EdgeNodeStats_t;

/* ---------- API ---------- */

/**
 * Initialise the Edge Node subsystem.
 *
 * @param send_fn   RS-485 send callback (NULL for standalone testing).
 * @param auth_ctx  Edge attestation context. NULL is rejected (parameter error,
 *                  fail-closed — no auth = no data). EDGE_AUTH_UNGATED_TESTING_ONLY
 *                  explicitly disables attestation gating for test-only use.
 *                  When non-NULL (and not the sentinel), edge_node_read_and_send()
 *                  checks edge_auth_is_authenticated() before sending data.
 */
void edge_node_init(RS485SendFunc_t send_fn, EdgeAuthContext_t *auth_ctx);

/**
 * Perform one read-validate-send cycle.
 * Reads from the active sensor driver, validates, encodes an RS-485
 * frame, and sends it to the Gateway.
 *
 * @param current_ms  Current monotonic timestamp.
 * @return Result of the cycle.
 */
EdgeNodeResult_t edge_node_read_and_send(uint64_t current_ms);

/**
 * Get Edge Node statistics.
 *
 * @return Pointer to the stats struct (read-only).
 */
const EdgeNodeStats_t *edge_node_get_stats(void);

/**
 * Reset statistics (e.g. for testing).
 */
void edge_node_reset_stats(void);

#endif /* EDGE_NODE_H */
