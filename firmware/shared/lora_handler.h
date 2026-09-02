/**
 * @file lora_handler.h
 * @brief LoRa handler — Gateway ↔ Hub (long-range, low-power).
 *
 * Architecture ref: Section 3, Tier 2↔3.
 * Used by: Industrial (primary), Agriculture (primary).
 *
 * This is a CORE handler, not Agriculture-specific. Industrial will
 * reuse this same module. Keep it domain-agnostic.
 *
 * IMPLEMENTATION STATUS (Phase 4):
 *   ✅ REAL: State machine, packet framing (header+CRC), sequence numbering,
 *           ACK/retry logic, regional config validation, CRC-16 integrity
 *   🔲 MOCKED (pending hardware): Radio transceive (SX127x/E22 SPI driver)
 *
 * PRE-FIELD-DEPLOYMENT ACTION ITEM:
 *   - Integrate with SX127x/E22 LoRa module SPI driver
 *   - Replace mock_radio_send/mock_radio_receive with real radio I/O
 *   - Validate on real hardware with actual LoRa link
 */

#ifndef LORA_HANDLER_H
#define LORA_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---------- Constants ---------- */

#define LORA_MAX_PAYLOAD      255  /**< Maximum LoRa payload size. */
#define LORA_MAX_SPREADING    12   /**< Maximum spreading factor. */
#define LORA_DEFAULT_CHANNEL  0    /**< Default frequency channel. */

/** LoRa packet overhead: header(8) + CRC(2) = 10 bytes. */
#define LORA_PACKET_OVERHEAD  10

/** Maximum retries for unacknowledged packets. */
#define LORA_MAX_RETRIES      3

/** Default ACK timeout in milliseconds. */
#define LORA_ACK_TIMEOUT_MS   5000

/* ---------- Types ---------- */

/**
 * LoRa radio configuration.
 */
typedef struct {
    uint8_t  spreading_factor;   /**< 7-12 (higher = longer range, slower). */
    uint8_t  bandwidth;          /**< 0=125kHz, 1=250kHz, 2=500kHz. */
    uint8_t  coding_rate;        /**< 1-4 (5/8 to 5/11). */
    uint8_t  tx_power_dbm;       /**< Transmit power in dBm (2-20). */
    uint16_t frequency_mhz;      /**< Frequency in MHz (e.g. 868, 915). */
    bool     crc_enabled;        /**< Enable CRC checking. */
} LoRaConfig_t;

/**
 * LoRa packet types.
 */
typedef enum {
    LORA_MSG_DATA = 0x01,        /**< Data frame. */
    LORA_MSG_ACK  = 0x02,        /**< Acknowledgement. */
    LORA_MSG_NACK = 0x03         /**< Negative acknowledgement. */
} LoRaMsgType_t;

/**
 * LoRa packet header (on-the-wire format).
 *
 * Layout: [dest_addr(2)] [src_addr(2)] [seq_num(1)] [msg_type(1)]
 *         [payload_len(1)] [flags(1)] [payload(N)] [crc16(2)]
 *
 * Total header: 8 bytes + payload + 2 bytes CRC = 10 + payload_len bytes.
 */
typedef struct __attribute__((packed)) {
    uint16_t dest_addr;          /**< Destination node address. */
    uint16_t src_addr;           /**< Source node address. */
    uint8_t  seq_num;            /**< Sequence number (wraps at 255). */
    uint8_t  msg_type;           /**< LoRaMsgType_t value. */
    uint8_t  payload_len;        /**< Length of payload that follows. */
    uint8_t  flags;              /**< Reserved flags byte. */
} LoRaPacketHeader_t;

/**
 * LoRa data frame (application-level view).
 */
typedef struct {
    uint16_t src_addr;            /**< Source node address. */
    uint16_t dest_addr;           /**< Destination address. */
    int16_t  rssi;                /**< Received signal strength (dBm). */
    float    snr;                 /**< Signal-to-noise ratio (dB). */
    uint8_t  seq_num;             /**< Packet sequence number. */
    uint8_t  payload_len;
    uint8_t  payload[LORA_MAX_PAYLOAD];
} LoRaFrame_t;

/**
 * LoRa handler result codes.
 */
typedef enum {
    LORA_OK,
    LORA_ERR_NOT_INIT,
    LORA_ERR_SEND_FAILED,
    LORA_ERR_NO_DATA,
    LORA_ERR_PARAM_NULL,
    LORA_ERR_BUSY,
    LORA_ERR_CRC_FAIL,           /**< Received packet CRC mismatch. */
    LORA_ERR_ACK_TIMEOUT,        /**< No ACK received within retry window. */
    LORA_ERR_NO_SPACE,           /**< Payload exceeds max size. */
    LORA_ERR_NOT_JOINED,         /**< Module attestation not yet completed. */
    LORA_ERR_JOIN_FAILED         /**< Module attestation failed. */
} LoRaResult_t;

/**
 * LoRa retry statistics (for diagnostics).
 */
typedef struct {
    uint32_t tx_packets;          /**< Total packets transmitted. */
    uint32_t tx_retries;          /**< Total retry attempts. */
    uint32_t tx_acks_received;    /**< ACKs received. */
    uint32_t tx_ack_timeouts;    /**< ACK timeouts (retries exhausted). */
    uint32_t rx_packets;          /**< Total packets received. */
    uint32_t rx_crc_errors;      /**< Packets with CRC mismatch. */
    uint32_t join_attempts;       /**< Module attestation attempts. */
    uint32_t join_failures;       /**< Module attestation failures. */
} LoRaStats_t;

/* ---------- API ---------- */

/**
 * Initialise the LoRa handler.
 * After init, the handler is in JOINING state — lora_join() must be
 * called to perform module attestation before any send/receive.
 *
 * @param config  Radio configuration (NULL for defaults).
 * @return true on success.
 */
bool lora_init(const LoRaConfig_t *config);

/**
 * Perform module attestation to join the LoRa network.
 *
 * The radio module (node_id 0xF0) must present a valid attestation
 * response before the handler transitions to JOINED state. Until
 * joined, lora_send() returns LORA_ERR_NOT_JOINED.
 *
 * Architecture ref: Section 4, Baseline — attestation is mandatory
 * before any data exchange.
 *
 * @param module_key  Pre-shared key for the radio module (node_id 0xF0).
 * @param key_len     Length of key (must be ATTESTATION_KEY_SIZE = 32).
 * @return LORA_OK on successful attestation, LORA_ERR_JOIN_FAILED otherwise.
 */
LoRaResult_t lora_join(const uint8_t *module_key, size_t key_len);

/**
 * Send data over LoRa with ACK/retry.
 *
 * Builds a properly formatted LoRa packet with header, CRC-16,
 * and transmits via the radio layer. If the peer sends an ACK,
 * the send is considered complete. If no ACK is received, retries
 * up to LORA_MAX_RETRIES times.
 *
 * @param dest_addr   Destination node address.
 * @param payload     Data to send.
 * @param payload_len Length of data.
 * @return LORA_OK on success (ACK received), LORA_ERR_ACK_TIMEOUT
 *         if all retries exhausted, or other error.
 */
LoRaResult_t lora_send(
    uint16_t dest_addr,
    const uint8_t *payload,
    uint8_t payload_len
);

/**
 * Receive data (non-blocking poll).
 *
 * Parses incoming packet, validates CRC-16, strips header, and
 * returns the application payload. Automatically sends ACK for
 * DATA packets.
 *
 * @param out_frame  Output: received frame (with header stripped).
 * @return LORA_OK if valid data was available, LORA_ERR_NO_DATA
 *         otherwise, LORA_ERR_CRC_FAIL if a corrupt packet was received.
 */
LoRaResult_t lora_receive(LoRaFrame_t *out_frame);

/**
 * Check if the LoRa radio is ready (not busy transmitting).
 *
 * @return true if ready.
 */
bool lora_is_ready(void);

/**
 * Get the current RSSI of the last received packet.
 *
 * @return RSSI in dBm (0 if no packet received).
 */
int16_t lora_get_rssi(void);

/**
 * Create a default LoRa configuration for a given region.
 *
 * @param region  "EU868" or "US915" (others default to EU868).
 * @return Default configuration for that region.
 */
LoRaConfig_t lora_default_config(const char *region);

/**
 * Get packet statistics (for diagnostics/telemetry).
 *
 * @param stats  Output: current statistics.
 */
void lora_get_stats(LoRaStats_t *stats);

/**
 * Get the current active radio configuration.
 *
 * @return Pointer to the active config (read-only).
 */
const LoRaConfig_t *lora_get_config(void);

/**
 * Compute CRC-16/CCITT over a data buffer.
 * Used internally for packet integrity and exposed for testing.
 *
 * @param data  Data buffer.
 * @param len   Length of data.
 * @return CRC-16 value.
 */
uint16_t lora_crc16(const uint8_t *data, size_t len);

/**
 * Configure mock ACK behaviour for testing.
 *
 * When fail_count > 0, the mock will return false (no ACK) for the
 * next fail_count calls to mock_radio_wait_ack, then resume normal
 * behaviour (returning true). Setting fail_count = 0 restores the
 * default loopback behaviour.
 *
 * @param fail_count  Number of subsequent ACK failures to simulate.
 */
void lora_mock_set_ack_fail_count(uint8_t fail_count);

/**
 * Set the mock radio module's stored key for testing.
 *
 * In production, this key is burned into the radio module at factory time.
 * For testing, this simulates the module having its own independent key.
 * If not set, lora_join() falls back to using the caller-provided key
 * (backward compatible, but doesn't test key-mismatch detection).
 *
 * @param key  32-byte key the mock module stores.
 */
void lora_mock_set_module_key(const uint8_t key[32]);

/**
 * Check if the LoRa handler has completed module attestation.
 *
 * @return true if joined (attestation successful).
 */
bool lora_is_joined(void);

#endif /* LORA_HANDLER_H */
