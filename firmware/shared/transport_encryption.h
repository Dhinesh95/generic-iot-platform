/**
 * @file transport_encryption.h
 * @brief Transport encryption — AES-128-CCM with per-session keys.
 *
 * Architecture ref: Section 4, Baseline (mandatory, all domains).
 * Threat addressed: T2 (wireless MITM).
 *
 * This is a clean-room implementation.
 */

#ifndef TRANSPORT_ENCRYPTION_H
#define TRANSPORT_ENCRYPTION_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---------- Constants ---------- */

#define TRANSPORT_AES_KEY_SIZE      16   /**< AES-128 key length. */
#define TRANSPORT_AES_BLOCK_SIZE    16   /**< AES block size. */
#define TRANSPORT_NONCE_SIZE        13   /**< CCM recommended nonce size. */
#define TRANSPORT_MIC_SIZE          4    /**< CCM authentication tag (MIC) length. */
#define TRANSPORT_MAX_PAYLOAD       256  /**< Maximum encrypted payload size. */
#define TRANSPORT_SESSION_KEY_TTL_MS  3600000  /**< Session key lifetime: 1 hour. */

/* ---------- Types ---------- */

/**
 * Per-session encryption context.
 * Created during the handshake phase; each session gets unique keys.
 */
typedef struct {
    uint8_t  session_id;                     /**< Monotonically incrementing session ID. */
    uint8_t  local_node_id;                  /**< This node's ID. */
    uint8_t  remote_node_id;                 /**< Peer node's ID. */
    uint8_t  tx_key[TRANSPORT_AES_KEY_SIZE]; /**< TX direction key. */
    uint8_t  rx_key[TRANSPORT_AES_KEY_SIZE]; /**< RX direction key. */
    uint32_t tx_nonce_counter;               /**< Nonce counter for TX (monotonic). */
    uint32_t rx_nonce_counter;               /**< Nonce counter for RX (monotonic). */
    uint64_t created_ms;                     /**< Session creation timestamp. */
    bool     active;                         /**< false = session expired/invalidated. */
} TransportSession_t;

/**
 * Encrypted frame on the wire.
 * Layout: [session_id(1)] [nonce_counter(4)] [mic(4)] [ciphertext(1..N)]
 */
typedef struct {
    uint8_t  session_id;                                 /**< Identifies the session. */
    uint32_t nonce_counter;                              /**< Nonce counter for this frame. */
    uint8_t  mic[TRANSPORT_MIC_SIZE];                    /**< Message integrity code. */
    uint16_t ciphertext_len;                             /**< Length of ciphertext that follows. */
    uint8_t  ciphertext[TRANSPORT_MAX_PAYLOAD];          /**< Encrypted payload. */
} TransportFrame_t;

/**
 * Result of a decryption / verification operation.
 */
typedef enum {
    TRANSPORT_DECRYPT_OK,             /**< Frame decrypted and MIC verified. */
    TRANSPORT_DECRYPT_ERR_MIC_FAIL,   /**< MIC mismatch — possible tampering. */
    TRANSPORT_DECRYPT_ERR_SESSION,    /**< Unknown or expired session. */
    TRANSPORT_DECRYPT_ERR_NONCE,      /**< Nonce counter out of order (replay). */
    TRANSPORT_DECRYPT_ERR_PARAM_NULL, /**< NULL pointer argument. */
    TRANSPORT_DECRYPT_ERR_TOO_LONG    /**< Payload exceeds max size. */
} TransportDecryptResult_t;

/* ---------- API ---------- */

/**
 * Initialise the transport encryption subsystem.
 *
 * @return true on success.
 */
bool transport_init(void);

/**
 * Create a new session between two nodes.
 *
 * @param session        Output session context (caller-allocated).
 * @param local_node_id  This node's ID.
 * @param remote_node_id Peer node's ID.
 * @param shared_key     Key material derived from attestation (32 bytes).
 * @return true on success.
 */
bool transport_create_session(
    TransportSession_t *session,
    uint8_t local_node_id,
    uint8_t remote_node_id,
    const uint8_t shared_key[TRANSPORT_AES_KEY_SIZE]
);

/**
 * Check whether a session is still valid (not expired).
 *
 * @param session    Session to check.
 * @param current_ms Current monotonic timestamp.
 * @return true if session is active and within TTL.
 */
bool transport_session_valid(const TransportSession_t *session, uint64_t current_ms);

/**
 * Encrypt a payload into a frame.
 *
 * @param session     Active session.
 * @param plaintext   Data to encrypt.
 * @param plaintext_len  Length of plaintext.
 * @param out_frame   Output: the encrypted frame.
 * @return true on success.
 */
bool transport_encrypt(
    TransportSession_t *session,
    const uint8_t *plaintext, size_t plaintext_len,
    TransportFrame_t *out_frame
);

/**
 * Decrypt a received frame and verify its MIC.
 *
 * @param session       Active session.
 * @param frame         Received frame.
 * @param out_plaintext Output buffer (must be >= TRANSPORT_MAX_PAYLOAD).
 * @param out_len       Output: length of decrypted plaintext.
 * @return TRANSPORT_DECRYPT_OK on success.
 */
TransportDecryptResult_t transport_decrypt(
    TransportSession_t *session,
    const TransportFrame_t *frame,
    uint8_t *out_plaintext,
    size_t *out_len
);

/**
 * Invalidate (tear down) a session.
 *
 * @param session Session to invalidate.
 */
void transport_session_invalidate(TransportSession_t *session);

/**
 * Derive per-session TX/RX keys from a shared key, canonical salt,
 * and direction labels.
 *
 * The salt must be the SAME for both peers (canonical ordering of
 * node IDs). The direction labels distinguish TX from RX so that
 * Node A's TX key == Node B's RX key.
 *
 * @param shared_key    Base shared key (from attestation).
 * @param salt          Canonical session salt (sorted node IDs).
 * @param salt_len      Length of salt.
 * @param out_tx_key    Output: TX direction key.
 * @param out_rx_key    Output: RX direction key.
 * @param tx_direction  Direction label for TX (e.g. "a_to_b").
 * @param rx_direction  Direction label for RX (e.g. "b_to_a").
 */
void transport_derive_keys(
    const uint8_t shared_key[TRANSPORT_AES_KEY_SIZE],
    const uint8_t *salt, size_t salt_len,
    uint8_t out_tx_key[TRANSPORT_AES_KEY_SIZE],
    uint8_t out_rx_key[TRANSPORT_AES_KEY_SIZE],
    const char *tx_direction,
    const char *rx_direction
);

#endif /* TRANSPORT_ENCRYPTION_H */
