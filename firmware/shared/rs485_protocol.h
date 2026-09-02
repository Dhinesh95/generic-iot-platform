/**
 * @file rs485_protocol.h
 * @brief RS-485 protocol handler — Edge ↔ Gateway (wired sensors).
 *
 * Architecture ref: Section 3, Tier 1↔2.
 * Used by: Industrial domain (primary), Home/Building (if wired sensors).
 *
 * Frame format with CRC-16 check. Clean-room implementation.
 */

#ifndef RS485_PROTOCOL_H
#define RS485_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---------- Constants ---------- */

#define RS485_MAX_FRAME_PAYLOAD  128  /**< Maximum payload per frame. */
#define RS485_BROADCAST_ADDR     0xFF /**< Broadcast address. */
#define RS485_CRC_POLYNOMIAL     0xA001 /**< CRC-16/MODBUS polynomial. */

/* ---------- Frame format ---------- */

/**
 * RS-485 frame layout:
 *   [START(1)] [DEST(1)] [SRC(1)] [LEN(1)] [PAYLOAD(N)] [CRC16(2)]
 *
 * START: 0xAA (sync byte)
 * DEST: destination node address
 * SRC:  source node address
 * LEN:  payload length (0..RS485_MAX_FRAME_PAYLOAD)
 * CRC16: CRC-16 over DEST+SRC+LEN+PAYLOAD (big-endian)
 */
typedef struct {
    uint8_t  dest_addr;
    uint8_t  src_addr;
    uint8_t  payload_len;
    uint8_t  payload[RS485_MAX_FRAME_PAYLOAD];
} RS485Frame_t;

/**
 * Result of frame parsing / validation.
 */
typedef enum {
    RS485_OK,               /**< Frame parsed and CRC valid. */
    RS485_ERR_CRC,          /**< CRC mismatch. */
    RS485_ERR_TOO_SHORT,    /**< Frame too short for header. */
    RS485_ERR_TOO_LONG,     /**< Payload length exceeds maximum. */
    RS485_ERR_BAD_START,    /**< Invalid start byte. */
    RS485_ERR_PARAM_NULL    /**< NULL pointer argument. */
} RS485ParseResult_t;

/* ---------- API ---------- */

/**
 * Compute CRC-16 (MODBUS variant) over a buffer.
 *
 * @param data  Data to compute CRC over.
 * @param len   Length of data.
 * @return CRC-16 value.
 */
uint16_t rs485_crc16(const uint8_t *data, size_t len);

/**
 * Encode a frame into a wire buffer.
 *
 * @param frame      The frame to encode.
 * @param out_buf    Output wire buffer.
 * @param buf_size   Size of the output buffer.
 * @param out_len    Output: number of bytes written.
 * @return true on success.
 */
bool rs485_encode(const RS485Frame_t *frame, uint8_t *out_buf, size_t buf_size, size_t *out_len);

/**
 * Parse a wire buffer into a frame, verifying CRC.
 *
 * @param wire_buf   Raw bytes received from RS-485.
 * @param wire_len   Number of bytes received.
 * @param out_frame  Output: parsed frame.
 * @return RS485_OK if the frame is valid.
 */
RS485ParseResult_t rs485_parse(const uint8_t *wire_buf, size_t wire_len, RS485Frame_t *out_frame);

/**
 * Create a frame ready for transmission.
 *
 * @param out_frame     Output frame.
 * @param dest_addr     Destination node address.
 * @param src_addr      Source node address.
 * @param payload       Payload data.
 * @param payload_len   Payload length.
 */
void rs485_create_frame(
    RS485Frame_t *out_frame,
    uint8_t dest_addr,
    uint8_t src_addr,
    const uint8_t *payload,
    uint8_t payload_len
);

#endif /* RS485_PROTOCOL_H */
