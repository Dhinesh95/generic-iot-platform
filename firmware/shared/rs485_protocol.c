/**
 * @file rs485_protocol.c
 * @brief RS-485 protocol handler implementation.
 *
 * Architecture ref: Section 3, Tier 1↔2.
 */

#include "rs485_protocol.h"
#include <string.h>

#define RS485_START_BYTE  0xAA
#define RS485_HEADER_SIZE  4   /* START + DEST + SRC + LEN */
#define RS485_CRC_SIZE     2
#define RS485_MIN_FRAME   (RS485_HEADER_SIZE + RS485_CRC_SIZE)

uint16_t rs485_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ RS485_CRC_POLYNOMIAL;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

bool rs485_encode(const RS485Frame_t *frame, uint8_t *out_buf, size_t buf_size, size_t *out_len)
{
    if (!frame || !out_buf || !out_len) return false;

    size_t total = RS485_HEADER_SIZE + frame->payload_len + RS485_CRC_SIZE;
    if (total > buf_size) return false;
    if (frame->payload_len > RS485_MAX_FRAME_PAYLOAD) return false;

    size_t pos = 0;

    /* Start byte. */
    out_buf[pos++] = RS485_START_BYTE;

    /* Header. */
    out_buf[pos++] = frame->dest_addr;
    out_buf[pos++] = frame->src_addr;
    out_buf[pos++] = frame->payload_len;

    /* Payload. */
    memcpy(out_buf + pos, frame->payload, frame->payload_len);
    pos += frame->payload_len;

    /* CRC-16 over DEST+SRC+LEN+PAYLOAD. */
    uint16_t crc = rs485_crc16(out_buf + 1, 3 + frame->payload_len);
    out_buf[pos++] = (crc >> 8) & 0xFF;  /* CRC high byte. */
    out_buf[pos++] = crc & 0xFF;         /* CRC low byte. */

    *out_len = pos;
    return true;
}

RS485ParseResult_t rs485_parse(const uint8_t *wire_buf, size_t wire_len, RS485Frame_t *out_frame)
{
    if (!wire_buf || !out_frame) return RS485_ERR_PARAM_NULL;

    if (wire_len < RS485_MIN_FRAME) return RS485_ERR_TOO_SHORT;

    /* Check start byte. */
    if (wire_buf[0] != RS485_START_BYTE) return RS485_ERR_BAD_START;

    /* Extract header. */
    uint8_t dest = wire_buf[1];
    uint8_t src  = wire_buf[2];
    uint8_t plen = wire_buf[3];

    (void)dest;
    (void)src;

    /* Validate payload length. */
    size_t expected_total = RS485_HEADER_SIZE + plen + RS485_CRC_SIZE;
    if (wire_len < expected_total) return RS485_ERR_TOO_SHORT;
    if (plen > RS485_MAX_FRAME_PAYLOAD) return RS485_ERR_TOO_LONG;

    /* Verify CRC-16. */
    uint16_t received_crc = ((uint16_t)wire_buf[RS485_HEADER_SIZE + plen] << 8)
                          | (uint16_t)wire_buf[RS485_HEADER_SIZE + plen + 1];
    uint16_t computed_crc = rs485_crc16(wire_buf + 1, 3 + plen);

    if (received_crc != computed_crc) return RS485_ERR_CRC;

    /* Populate output frame. */
    out_frame->dest_addr = wire_buf[1];
    out_frame->src_addr  = wire_buf[2];
    out_frame->payload_len = plen;
    memcpy(out_frame->payload, wire_buf + RS485_HEADER_SIZE, plen);

    return RS485_OK;
}

void rs485_create_frame(
    RS485Frame_t *out_frame,
    uint8_t dest_addr,
    uint8_t src_addr,
    const uint8_t *payload,
    uint8_t payload_len)
{
    if (!out_frame) return;

    out_frame->dest_addr = dest_addr;
    out_frame->src_addr  = src_addr;
    out_frame->payload_len = payload_len;
    if (payload && payload_len > 0 && payload_len <= RS485_MAX_FRAME_PAYLOAD) {
        memcpy(out_frame->payload, payload, payload_len);
    }
}
