/**
 * @file transport_encryption.c
 * @brief Transport encryption — AES-128-CCM with per-session keys.
 *
 * Architecture ref: Section 4, Baseline (mandatory, all domains).
 *
 * Implementation uses mbedTLS for AES-128-CCM (true CCM mode —
 * Counter-mode encryption + CBC-MAC authentication in a single pass).
 *
 * The previous implementation used a reference AES-CTR + HMAC-truncated-MIC
 * which was NOT true CCM. This version uses mbedTLS's
 * mbedtls_ccm_encrypt_and_tag / mbedtls_ccm_auth_decrypt which implement
 * the standard NIST SP 800-38C / RFC 3610 CCM mode.
 */

#include "transport_encryption.h"
#include "attestation.h"  /* for iot_hmac_sha256 used in key derivation */
#include <string.h>

#include "mbedtls/ccm.h"
#include "mbedtls/aes.h"

/* ---------- Internal state ---------- */

static bool g_initialised = false;
static uint8_t g_next_session_id = 0;

/* ---------- Public API ---------- */

bool transport_init(void)
{
    g_initialised = true;
    g_next_session_id = 1;
    return true;
}

bool transport_create_session(
    TransportSession_t *session,
    uint8_t local_node_id,
    uint8_t remote_node_id,
    const uint8_t shared_key[TRANSPORT_AES_KEY_SIZE])
{
    if (!session || !shared_key) return false;
    if (!g_initialised) return false;

    memset(session, 0, sizeof(TransportSession_t));
    session->session_id = g_next_session_id++;
    session->local_node_id = local_node_id;
    session->remote_node_id = remote_node_id;
    session->tx_nonce_counter = 0;
    session->rx_nonce_counter = 0;
    session->created_ms = 0;
    session->active = true;

    /*
     * Derive TX/RX keys using canonical (sorted) node pair + direction.
     * This ensures Node A's TX key == Node B's RX key, and vice versa.
     *
     * Salt = [min(A,B), max(A,B)] — same for both sides.
     * Direction "a_to_b" = from lower-ID to higher-ID node.
     */
    uint8_t lower = (local_node_id < remote_node_id) ? local_node_id : remote_node_id;
    uint8_t upper = (local_node_id < remote_node_id) ? remote_node_id : local_node_id;
    bool local_is_lower = (local_node_id == lower);

    uint8_t salt[2];
    salt[0] = lower;
    salt[1] = upper;

    if (local_is_lower) {
        transport_derive_keys(shared_key, salt, 2,
                              session->tx_key, session->rx_key,
                              "a_to_b", "b_to_a");
    } else {
        transport_derive_keys(shared_key, salt, 2,
                              session->tx_key, session->rx_key,
                              "b_to_a", "a_to_b");
    }

    return true;
}

bool transport_session_valid(const TransportSession_t *session, uint64_t current_ms)
{
    if (!session) return false;
    if (!session->active) return false;
    if (current_ms < session->created_ms) return false;
    if ((current_ms - session->created_ms) > TRANSPORT_SESSION_KEY_TTL_MS) return false;
    return true;
}

/**
 * Build the CCM nonce from session_id + nonce_counter.
 * CCM nonce is 13 bytes (TRANSPORT_NONCE_SIZE).
 * Layout: [session_id(1)] [nonce_counter(4)] [zero_pad(8)]
 */
static void build_ccm_nonce(
    uint8_t nonce[TRANSPORT_NONCE_SIZE],
    uint8_t session_id,
    uint32_t nonce_counter)
{
    memset(nonce, 0, TRANSPORT_NONCE_SIZE);
    nonce[0] = session_id;
    nonce[1] = (nonce_counter >> 24) & 0xff;
    nonce[2] = (nonce_counter >> 16) & 0xff;
    nonce[3] = (nonce_counter >> 8)  & 0xff;
    nonce[4] = (nonce_counter)       & 0xff;
}

bool transport_encrypt(
    TransportSession_t *session,
    const uint8_t *plaintext, size_t plaintext_len,
    TransportFrame_t *out_frame)
{
    if (!session || !plaintext || !out_frame) return false;
    if (!session->active) return false;
    if (plaintext_len > TRANSPORT_MAX_PAYLOAD) return false;

    /* Build CCM nonce. */
    uint8_t nonce[TRANSPORT_NONCE_SIZE];
    build_ccm_nonce(nonce, session->session_id, session->tx_nonce_counter);

    /*
     * Use mbedTLS AES-128-CCM for authenticated encryption.
     *
     * CCM tag length = TRANSPORT_MIC_SIZE (4 bytes). This is the minimum
     * allowed by NIST SP 800-38C for CCM. The tag is appended to the
     * ciphertext and placed in out_frame->mic.
     *
     * No additional authenticated data (AAD) is used in this frame format —
     * the session_id and nonce_counter are embedded in the nonce itself.
     */
    mbedtls_ccm_context ccm_ctx;
    mbedtls_ccm_init(&ccm_ctx);

    int ret = mbedtls_ccm_setkey(&ccm_ctx, MBEDTLS_CIPHER_ID_AES,
                                  session->tx_key, TRANSPORT_AES_KEY_SIZE * 8);
    if (ret != 0) {
        mbedtls_ccm_free(&ccm_ctx);
        return false;
    }

    /*
     * mbedtls_ccm_encrypt_and_tag encrypts in-place and appends the tag.
     * We provide output buffer = ciphertext || tag (but we split them).
     * Actually, mbedTLS CCM encrypt outputs: ciphertext || tag.
     * We need ciphertext in out_frame->ciphertext and tag in out_frame->mic.
     * So we use a temp buffer, then split.
     */
    uint8_t enc_buf[TRANSPORT_MAX_PAYLOAD + TRANSPORT_MIC_SIZE];
    memset(enc_buf, 0, sizeof(enc_buf));

    ret = mbedtls_ccm_encrypt_and_tag(
        &ccm_ctx,
        plaintext_len,              /* length of plaintext */
        nonce, TRANSPORT_NONCE_SIZE,/* nonce */
        NULL, 0,                    /* No AAD */
        plaintext,                  /* input (plaintext) */
        enc_buf,                    /* output (ciphertext) */
        enc_buf + plaintext_len,    /* tag output (appended after ciphertext) */
        TRANSPORT_MIC_SIZE          /* tag length */
    );

    mbedtls_ccm_free(&ccm_ctx);

    if (ret != 0) {
        return false;
    }

    /* Copy ciphertext to frame. */
    memcpy(out_frame->ciphertext, enc_buf, plaintext_len);
    /* Copy MIC (tag) to frame. */
    memcpy(out_frame->mic, enc_buf + plaintext_len, TRANSPORT_MIC_SIZE);

    out_frame->ciphertext_len = (uint16_t)plaintext_len;
    out_frame->session_id = session->session_id;
    out_frame->nonce_counter = session->tx_nonce_counter;

    session->tx_nonce_counter++;
    return true;
}

TransportDecryptResult_t transport_decrypt(
    TransportSession_t *session,
    const TransportFrame_t *frame,
    uint8_t *out_plaintext,
    size_t *out_len)
{
    if (!session || !frame || !out_plaintext || !out_len)
        return TRANSPORT_DECRYPT_ERR_PARAM_NULL;
    if (!session->active)
        return TRANSPORT_DECRYPT_ERR_SESSION;
    if (frame->nonce_counter < session->rx_nonce_counter)
        return TRANSPORT_DECRYPT_ERR_NONCE;
    if (frame->ciphertext_len > TRANSPORT_MAX_PAYLOAD)
        return TRANSPORT_DECRYPT_ERR_PARAM_NULL;

    /* Build CCM nonce using the frame's session_id (sender's). */
    uint8_t nonce[TRANSPORT_NONCE_SIZE];
    build_ccm_nonce(nonce, frame->session_id, frame->nonce_counter);

    /*
     * Use mbedTLS AES-128-CCM for authenticated decryption.
     *
     * The ciphertext + tag are provided separately:
     * - ciphertext: frame->ciphertext (frame->ciphertext_len bytes)
     * - tag: frame->mic (TRANSPORT_MIC_SIZE bytes)
     *
     * mbedTLS CCM decrypt expects: ciphertext || tag concatenated.
     * We assemble them in a temp buffer.
     */
    mbedtls_ccm_context ccm_ctx;
    mbedtls_ccm_init(&ccm_ctx);

    int ret = mbedtls_ccm_setkey(&ccm_ctx, MBEDTLS_CIPHER_ID_AES,
                                  session->rx_key, TRANSPORT_AES_KEY_SIZE * 8);
    if (ret != 0) {
        mbedtls_ccm_free(&ccm_ctx);
        return TRANSPORT_DECRYPT_ERR_MIC_FAIL;
    }

    /* Concatenate ciphertext || tag for mbedTLS. */
    uint8_t auth_buf[TRANSPORT_MAX_PAYLOAD + TRANSPORT_MIC_SIZE];
    memcpy(auth_buf, frame->ciphertext, frame->ciphertext_len);
    memcpy(auth_buf + frame->ciphertext_len, frame->mic, TRANSPORT_MIC_SIZE);

    ret = mbedtls_ccm_auth_decrypt(
        &ccm_ctx,
        frame->ciphertext_len,           /* length of ciphertext */
        nonce, TRANSPORT_NONCE_SIZE,     /* nonce */
        NULL, 0,                         /* No AAD */
        auth_buf,                        /* input (ciphertext || tag) */
        out_plaintext,                   /* output (decrypted plaintext) */
        auth_buf + frame->ciphertext_len, /* expected tag */
        TRANSPORT_MIC_SIZE               /* tag length */
    );

    mbedtls_ccm_free(&ccm_ctx);

    if (ret == MBEDTLS_ERR_CCM_AUTH_FAILED) {
        return TRANSPORT_DECRYPT_ERR_MIC_FAIL;
    }
    if (ret != 0) {
        return TRANSPORT_DECRYPT_ERR_MIC_FAIL;
    }

    *out_len = frame->ciphertext_len;
    session->rx_nonce_counter = frame->nonce_counter + 1;
    return TRANSPORT_DECRYPT_OK;
}

void transport_session_invalidate(TransportSession_t *session)
{
    if (session) {
        session->active = false;
        memset(session->tx_key, 0, TRANSPORT_AES_KEY_SIZE);
        memset(session->rx_key, 0, TRANSPORT_AES_KEY_SIZE);
    }
}

void transport_derive_keys(
    const uint8_t shared_key[TRANSPORT_AES_KEY_SIZE],
    const uint8_t *salt, size_t salt_len,
    uint8_t out_tx_key[TRANSPORT_AES_KEY_SIZE],
    uint8_t out_rx_key[TRANSPORT_AES_KEY_SIZE],
    const char *tx_direction,
    const char *rx_direction)
{
    /* HMAC outputs 32 bytes; we truncate to TRANSPORT_AES_KEY_SIZE (16). */
    uint8_t full_hmac[ATTESTATION_HMAC_SIZE];

    /* Derive TX key: HMAC-SHA256(shared_key, tx_direction || salt) */
    size_t tx_dir_len = strlen(tx_direction);
    size_t rx_dir_len = strlen(rx_direction);
    size_t copy_len = (salt_len > 30) ? 30 : salt_len;

    uint8_t tx_input[32 + 32];
    size_t tx_len = 0;
    memcpy(tx_input + tx_len, tx_direction, tx_dir_len);
    tx_len += tx_dir_len;
    memcpy(tx_input + tx_len, salt, copy_len);
    tx_len += copy_len;
    iot_hmac_sha256(shared_key, TRANSPORT_AES_KEY_SIZE, tx_input, tx_len, full_hmac);
    memcpy(out_tx_key, full_hmac, TRANSPORT_AES_KEY_SIZE);

    /* Derive RX key: HMAC-SHA256(shared_key, rx_direction || salt) */
    uint8_t rx_input[32 + 32];
    size_t rx_len = 0;
    memcpy(rx_input + rx_len, rx_direction, rx_dir_len);
    rx_len += rx_dir_len;
    memcpy(rx_input + rx_len, salt, copy_len);
    rx_len += copy_len;
    iot_hmac_sha256(shared_key, TRANSPORT_AES_KEY_SIZE, rx_input, rx_len, full_hmac);
    memcpy(out_rx_key, full_hmac, TRANSPORT_AES_KEY_SIZE);
}
