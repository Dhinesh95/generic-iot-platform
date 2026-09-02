/**
 * @file firmware_integrity.c
 * @brief Firmware integrity — HMAC-SHA256 verification implementation.
 *
 * Architecture ref: Section 4, Section 5.
 */

#include "firmware_integrity.h"
#include "attestation.h"  /* for hmac_sha256 */
#include <string.h>

/* ---------- Internal state ---------- */

static uint8_t g_hmac_key[FW_INTEGRITY_HMAC_SIZE];
static bool g_key_set = false;
static bool g_initialised = false;

/* ---------- Public API ---------- */

bool firmware_integrity_init(void)
{
    memset(g_hmac_key, 0, sizeof(g_hmac_key));
    g_key_set = false;
    g_initialised = true;
    return true;
}

bool firmware_integrity_set_key(const uint8_t *key, size_t key_len)
{
    if (!g_initialised) return false;
    if (!key || key_len != FW_INTEGRITY_HMAC_SIZE) return false;

    memcpy(g_hmac_key, key, FW_INTEGRITY_HMAC_SIZE);
    g_key_set = true;
    return true;
}

FirmwareCheckResult_t firmware_integrity_verify(
    const FirmwareImageHeader_t *header,
    const uint8_t *image_data,
    size_t image_data_len)
{
    if (!header || !image_data) return FW_CHECK_ERR_PARAM_NULL;
    if (!g_initialised) return FW_CHECK_ERR_NOT_INIT;
    if (!g_key_set) return FW_CHECK_ERR_NOT_INIT;

    /* Validate magic. */
    if (header->magic != FW_INTEGRITY_MANIFEST_MAGIC) {
        return FW_CHECK_ERR_MAGIC;
    }

    /* Validate image size. */
    if (header->image_size != image_data_len) {
        return FW_CHECK_ERR_SIZE;
    }

    /* Compute HMAC over the image data. */
    uint8_t computed_hmac[FW_INTEGRITY_HMAC_SIZE];
    firmware_integrity_compute_hmac(
        g_hmac_key, FW_INTEGRITY_HMAC_SIZE,
        image_data, image_data_len,
        computed_hmac
    );

    /* Constant-time comparison. */
    uint8_t diff = 0;
    for (int i = 0; i < FW_INTEGRITY_HMAC_SIZE; i++) {
        diff |= header->hmac[i] ^ computed_hmac[i];
    }

    return (diff == 0) ? FW_CHECK_OK : FW_CHECK_ERR_HMAC;
}

void firmware_integrity_compute_hmac(
    const uint8_t *key, size_t key_len,
    const uint8_t *image_data, size_t image_data_len,
    uint8_t out_hmac[FW_INTEGRITY_HMAC_SIZE])
{
    iot_hmac_sha256(key, key_len, image_data, image_data_len, out_hmac);
}
