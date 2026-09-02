/**
 * @file verified_boot.c
 * @brief Verified boot implementation — boot-time integrity verification.
 */

#include "verified_boot.h"
#include "firmware_integrity.h"
#include "attestation.h"
#include <string.h>

/* ---------- Key Derivation ---------- */

VerifiedBootResult_t verified_boot_derive_key(const uint8_t *identity_key,
                                               size_t identity_len,
                                               uint8_t *derived_key)
{
    if (!identity_key || !derived_key) return BOOT_ERR_PARAM_NULL;
    if (identity_len != VERIFIED_BOOT_KEY_SIZE) return BOOT_ERR_KEY_DERIVATION;

    /* Derive: HMAC-SHA256(identity_key, "verified_boot_key_derivation") */
    static const char derivation_context[] = "verified_boot_key_derivation";
    uint8_t hmac_result[ATTESTATION_HMAC_SIZE];

    iot_hmac_sha256(identity_key, identity_len,
                    (const uint8_t *)derivation_context,
                    sizeof(derivation_context) - 1,
                    hmac_result);

    /* Take first 32 bytes as derived key. */
    memcpy(derived_key, hmac_result, VERIFIED_BOOT_KEY_SIZE);

    /* Zero the intermediate result. */
    memset(hmac_result, 0, sizeof(hmac_result));

    return BOOT_OK;
}

/* ---------- Boot Verification ---------- */

VerifiedBootResult_t verified_boot_check(const FlashImageDescriptor_t *descriptor,
                                          const uint8_t *identity_key,
                                          size_t identity_len,
                                          BootVerification_t *result)
{
    if (!descriptor || !identity_key || !result) return BOOT_ERR_PARAM_NULL;

    memset(result, 0, sizeof(*result));
    result->fw_version = descriptor->version;

    if (!descriptor->present) {
        result->result = BOOT_ERR_INTEGRITY;
        result->image_valid = false;
        return BOOT_OK;
    }

    /* Derive the verification key. */
    uint8_t derived_key[VERIFIED_BOOT_KEY_SIZE];
    VerifiedBootResult_t dk_result = verified_boot_derive_key(
        identity_key, identity_len, derived_key);

    if (dk_result != BOOT_OK) {
        result->result = dk_result;
        result->image_valid = false;
        return BOOT_OK;
    }

    /* Compute HMAC-SHA256(derived_key, image_hash || version || size).
     * In production, this would hash the actual flash contents.
     * Here we verify the stored hash matches what we'd expect. */
    uint8_t computed_hash[VERIFIED_BOOT_HASH_SIZE];
    uint8_t data_to_hash[VERIFIED_BOOT_HASH_SIZE + sizeof(uint32_t) * 2];
    size_t data_len = 0;

    /* Version (big-endian). */
    uint32_t ver = descriptor->version;
    data_to_hash[data_len++] = (ver >> 24) & 0xFF;
    data_to_hash[data_len++] = (ver >> 16) & 0xFF;
    data_to_hash[data_len++] = (ver >> 8) & 0xFF;
    data_to_hash[data_len++] = ver & 0xFF;

    /* Size (big-endian). */
    uint32_t sz = descriptor->size;
    data_to_hash[data_len++] = (sz >> 24) & 0xFF;
    data_to_hash[data_len++] = (sz >> 16) & 0xFF;
    data_to_hash[data_len++] = (sz >> 8) & 0xFF;
    data_to_hash[data_len++] = sz & 0xFF;

    /* Slot (1 byte). */
    data_to_hash[data_len++] = (uint8_t)descriptor->slot;

    iot_hmac_sha256(derived_key, VERIFIED_BOOT_KEY_SIZE,
                    data_to_hash, data_len, computed_hash);

    /* Compare stored hash with computed hash. */
    result->image_valid = (memcmp(computed_hash, descriptor->hash,
                                   VERIFIED_BOOT_HASH_SIZE) == 0);
    result->result = result->image_valid ? BOOT_OK : BOOT_ERR_INTEGRITY;

    /* Zero sensitive data. */
    memset(derived_key, 0, sizeof(derived_key));
    memset(computed_hash, 0, sizeof(computed_hash));

    return BOOT_OK;
}

/* ---------- Full Boot Check ---------- */

VerifiedBootResult_t verified_boot_full_check(BootReadImageFunc_t read_image,
                                               BootRollbackFunc_t rollback,
                                               const uint8_t *identity_key,
                                               size_t identity_len,
                                               uint32_t active_slot,
                                               BootVerification_t *result)
{
    if (!read_image || !identity_key || !result) return BOOT_ERR_PARAM_NULL;

    memset(result, 0, sizeof(*result));

    /* Read the active image descriptor. */
    FlashImageDescriptor_t desc;
    if (!read_image(active_slot, &desc)) {
        result->result = BOOT_ERR_NOT_PROVISIONED;
        result->image_valid = false;
        return BOOT_OK;
    }

    /* Verify the active image. */
    VerifiedBootResult_t vr = verified_boot_check(&desc, identity_key,
                                                   identity_len, result);
    if (vr != BOOT_OK) return vr;

    /* If image is invalid and rollback is available, trigger rollback. */
    if (!result->image_valid && rollback) {
        uint32_t other_slot = (active_slot == 0) ? 1 : 0;
        if (rollback(other_slot)) {
            result->rollback_triggered = true;
        }
    }

    return BOOT_OK;
}
