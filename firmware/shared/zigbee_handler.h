/**
 * @file zigbee_handler.h
 * @brief Zigbee handler — Gateway ↔ Hub (Home/Building domain).
 *
 * Architecture ref: Section 3, Home/Building row in Tier 2↔3 table.
 *
 * Basic join/send/receive. No advanced mesh routing optimization yet.
 * Clean-room implementation.
 */

#ifndef ZIGBEE_HANDLER_H
#define ZIGBEE_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---------- Constants ---------- */

#define ZIGBEE_MAX_PAYLOAD      72   /**< Maximum payload per Zigbee frame. */
#define ZIGBEE_SHORT_ADDR_INVALID  0xFFFF
#define ZIGBEE_CHANNEL_DEFAULT  15   /**< Default Zigbee channel. */

/* ---------- Types ---------- */

/**
 * Zigbee node identity.
 */
typedef struct {
    uint16_t short_addr;       /**< Assigned short address. */
    uint64_t extended_addr;    /**< IEEE 64-bit extended address. */
    uint8_t  endpoint;         /**< Application endpoint. */
    uint8_t  node_id;          /**< Platform-level node ID. */
    bool     joined;           /**< true if currently joined to the network. */
} ZigbeeNodeInfo_t;

/**
 * Zigbee data frame.
 */
typedef struct {
    uint16_t src_addr;
    uint16_t dest_addr;
    uint8_t  src_endpoint;
    uint8_t  dest_endpoint;
    uint8_t  payload_len;
    uint8_t  payload[ZIGBEE_MAX_PAYLOAD];
} ZigbeeFrame_t;

/**
 * Zigbee handler result codes.
 */
typedef enum {
    ZIGBEE_OK,
    ZIGBEE_ERR_NOT_JOINED,
    ZIGBEE_ERR_SEND_FAILED,
    ZIGBEE_ERR_NO_DATA,
    ZIGBEE_ERR_PARAM_NULL
} ZigbeeResult_t;

/* ---------- API ---------- */

/**
 * Initialise the Zigbee handler.
 *
 * @param channel  Zigbee channel (11-26), or 0 for default.
 * @return true on success.
 */
bool zigbee_init(uint8_t channel);

/**
 * Join a Zigbee network.
 * In production this performs the actual Zigbee commissioning.
 * For testing, this simulates a successful join.
 *
 * @param node_info  Output: node identity after joining.
 * @return ZIGBEE_OK on success.
 */
ZigbeeResult_t zigbee_join(ZigbeeNodeInfo_t *node_info);

/**
 * Leave (disconnect from) the Zigbee network.
 *
 * @return ZIGBEE_OK on success.
 */
ZigbeeResult_t zigbee_leave(void);

/**
 * Send data to another node.
 *
 * @param dest_addr     Destination short address.
 * @param dest_endpoint Destination endpoint.
 * @param payload       Data to send.
 * @param payload_len   Length of data.
 * @return ZIGBEE_OK on success.
 */
ZigbeeResult_t zigbee_send(
    uint16_t dest_addr,
    uint8_t dest_endpoint,
    const uint8_t *payload,
    uint8_t payload_len
);

/**
 * Receive data (non-blocking poll).
 *
 * @param out_frame  Output: received frame.
 * @return ZIGBEE_OK if data was available, ZIGBEE_ERR_NO_DATA otherwise.
 */
ZigbeeResult_t zigbee_receive(ZigbeeFrame_t *out_frame);

/**
 * Check if currently joined to a network.
 *
 * @return true if joined.
 */
bool zigbee_is_joined(void);

#endif /* ZIGBEE_HANDLER_H */
