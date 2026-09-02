/**
 * @file ota_manifest.h
 * @brief OTA manifest — firmware package metadata and compatibility check.
 *
 * Architecture ref: Section 5 (OTA: canary rollout, image targeting).
 *
 * A FirmwareManifest_t represents an incoming OTA package's metadata.
 * The compatibility check ensures a firmware image is only accepted
 * by devices matching BOTH domain_profile_id AND hw_variant_id —
 * never partial matching.
 */

#ifndef OTA_MANIFEST_H
#define OTA_MANIFEST_H

#include <stdint.h>
#include <stdbool.h>
#include "device_identity.h"
#include "firmware_integrity.h"  /* for FW_INTEGRITY_HMAC_SIZE */

/* ---------- Types ---------- */

/**
 * Firmware manifest — metadata for an incoming OTA package.
 * This is NOT the firmware image itself — it's the header metadata
 * used for compatibility checking before the image is flashed.
 */
typedef struct {
    DomainProfileId_t target_domain_profile_id;  /**< Target domain profile. */
    uint8_t           target_hw_variant_id;       /**< Target hardware variant. */
    uint32_t          fw_version;                 /**< Firmware version in this package. */
    uint32_t          image_size;                 /**< Total image size in bytes. */
    uint8_t           image_hmac[FW_INTEGRITY_HMAC_SIZE]; /**< HMAC-SHA256 of image. */
} FirmwareManifest_t;

/**
 * Result of an OTA compatibility check.
 */
typedef enum {
    OTA_COMPAT_OK,                  /**< Manifest matches device identity — accept. */
    OTA_COMPAT_ERR_DOMAIN_MISMATCH, /**< Domain profile does not match. */
    OTA_COMPAT_ERR_VARIANT_MISMATCH, /**< Hardware variant does not match. */
    OTA_COMPAT_ERR_VERSION_DOWNGRADE, /**< Firmware version is older than current. */
    OTA_COMPAT_ERR_PARAM_NULL      /**< NULL pointer argument. */
} OtaCompatResult_t;

/* ---------- API ---------- */

/**
 * Check if a firmware manifest is compatible with a device identity.
 *
 * Returns OTA_COMPAT_OK only if BOTH domain_profile_id AND hw_variant_id
 * match. Any mismatch rejects — never partial matching.
 *
 * Also rejects version downgrades (manifest version must be >= device version).
 *
 * @param device    The device's identity.
 * @param manifest  The incoming OTA manifest.
 * @return OTA_COMPAT_OK if compatible, specific error otherwise.
 */
OtaCompatResult_t ota_compatibility_check(
    const DeviceIdentity_t *device,
    const FirmwareManifest_t *manifest
);

/**
 * Get a human-readable string for an OTA compatibility result.
 *
 * @param result  The result code.
 * @return Constant string describing the result.
 */
const char *ota_compat_result_str(OtaCompatResult_t result);

#endif /* OTA_MANIFEST_H */
