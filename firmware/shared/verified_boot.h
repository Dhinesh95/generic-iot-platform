/**
 * @file verified_boot.h
 * @brief Verified boot — boot-time integrity verification.
 *
 * Architecture ref: Phase 21 (Verified Boot Chain).
 *
 * Verifies firmware image integrity at boot using HMAC-SHA256.
 * Key is derived from device identity (not hardcoded).
 * If verification fails, the system rolls back to the previous slot.
 */

#ifndef VERIFIED_BOOT_H
#define VERIFIED_BOOT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---------- Constants ---------- */

#define VERIFIED_BOOT_HASH_SIZE    32   /**< SHA-256 digest size. */
#define VERIFIED_BOOT_KEY_SIZE     32   /**< HMAC key size. */
#define VERIFIED_BOOT_NONCE_SIZE   16   /**< Derivation nonce size. */

/* ---------- Types ---------- */

typedef enum {
    BOOT_OK = 0,
    BOOT_ERR_INTEGRITY,        /**< Image integrity check failed. */
    BOOT_ERR_KEY_DERIVATION,   /**< Key derivation failed. */
    BOOT_ERR_NOT_PROVISIONED,  /**< Device identity not provisioned. */
    BOOT_ERR_PARAM_NULL        /**< NULL pointer argument. */
} VerifiedBootResult_t;

/**
 * Boot verification result.
 */
typedef struct {
    VerifiedBootResult_t result;        /**< Overall result. */
    bool                 image_valid;   /**< Whether the image passed integrity check. */
    bool                 rollback_triggered; /**< Whether rollback was triggered. */
    uint32_t             fw_version;    /**< Version that was verified. */
} BootVerification_t;

/**
 * Flash image descriptor (what the boot checker reads).
 */
typedef struct {
    uint32_t slot;              /**< Which partition slot (0 or 1). */
    uint32_t size;              /**< Image size in bytes. */
    uint32_t version;           /**< Firmware version in this image. */
    uint8_t  hash[VERIFIED_BOOT_HASH_SIZE]; /**< Stored HMAC-SHA256. */
    bool     present;           /**< Whether an image is in this slot. */
} FlashImageDescriptor_t;

/**
 * Boot verification callback.
 * Called to read an image descriptor from flash.
 */
typedef bool (*BootReadImageFunc_t)(uint32_t slot, FlashImageDescriptor_t *desc);

/**
 * Rollback callback.
 * Called to switch the active boot slot back to the previous image.
 */
typedef bool (*BootRollbackFunc_t)(uint32_t previous_slot);

/* ---------- API ---------- */

/**
 * Verify the boot image. Reads the image descriptor, derives the
 * verification key from device identity, and checks the stored hash.
 *
 * @param descriptor   The image descriptor to verify.
 * @param identity_key The device's attestation key (32 bytes).
 * @param identity_len Length of identity key (must be 32).
 * @param result       Output: verification result.
 * @return BOOT_OK on success (check result->image_valid for pass/fail).
 */
VerifiedBootResult_t verified_boot_check(const FlashImageDescriptor_t *descriptor,
                                          const uint8_t *identity_key,
                                          size_t identity_len,
                                          BootVerification_t *result);

/**
 * Derive a boot-specific key from the device identity key.
 * Uses HMAC-SHA256(identity_key, "verified_boot_key_derivation").
 *
 * @param identity_key  Device attestation key (32 bytes).
 * @param identity_len  Length of identity key.
 * @param derived_key   Output: 32-byte derived key.
 * @return BOOT_OK on success.
 */
VerifiedBootResult_t verified_boot_derive_key(const uint8_t *identity_key,
                                               size_t identity_len,
                                               uint8_t *derived_key);

/**
 * Perform a full boot check with automatic rollback on failure.
 * Reads the current image, verifies it, and rolls back if invalid.
 *
 * @param read_image  Callback to read image descriptors from flash.
 * @param rollback    Callback to trigger rollback to previous slot.
 * @param identity_key Device attestation key.
 * @param identity_len Length of identity key.
 * @param active_slot  The slot we booted from.
 * @param result       Output: full boot verification result.
 * @return BOOT_OK on success.
 */
VerifiedBootResult_t verified_boot_full_check(BootReadImageFunc_t read_image,
                                               BootRollbackFunc_t rollback,
                                               const uint8_t *identity_key,
                                               size_t identity_len,
                                               uint32_t active_slot,
                                               BootVerification_t *result);

#endif /* VERIFIED_BOOT_H */
