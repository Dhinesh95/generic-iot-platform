/**
 * @file firmware_integrity.h
 * @brief Firmware integrity — HMAC-SHA256 verification.
 *
 * Architecture ref: Section 4 (Baseline: firmware HMAC verify),
 *                   Section 5 (OTA: HMAC-SHA256 firmware verification).
 *
 * CRC-32 is NOT implemented — there is no legacy image to be
 * backward-compatible with in this fresh project.
 */

#ifndef FIRMWARE_INTEGRITY_H
#define FIRMWARE_INTEGRITY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---------- Constants ---------- */

#define FW_INTEGRITY_HMAC_SIZE     32   /**< HMAC-SHA256 digest size. */
#define FW_INTEGRITY_MANIFEST_MAGIC 0x4657494D  /**< "FWIM" ASCII. */

/* ---------- Types ---------- */

/**
 * Firmware image header stored at the start of the firmware partition.
 * The HMAC is computed over the image bytes EXCLUDING this header;
 * the header carries the pre-computed HMAC for verification.
 */
typedef struct {
    uint32_t magic;                              /**< Must be FW_INTEGRITY_MANIFEST_MAGIC. */
    uint32_t image_size;                         /**< Size of firmware image in bytes. */
    uint32_t image_crc32;                        /**< CRC-32 (kept for future OTA manifest compat, not used for verification). */
    uint8_t  hmac[FW_INTEGRITY_HMAC_SIZE];       /**< HMAC-SHA256 over image bytes. */
    uint32_t version;                            /**< Firmware version number. */
    uint8_t  domain_profile_id;                  /**< Domain profile this image was built for. */
    uint8_t  hw_variant_id;                      /**< Hardware variant this image targets. */
    uint16_t reserved;                           /**< Alignment padding. */
} __attribute__((packed)) FirmwareImageHeader_t;

/**
 * Result of a firmware integrity check.
 */
typedef enum {
    FW_CHECK_OK,              /**< Firmware image is authentic and intact. */
    FW_CHECK_ERR_HMAC,        /**< HMAC verification failed — image may be corrupted or tampered. */
    FW_CHECK_ERR_MAGIC,       /**< Invalid header magic number. */
    FW_CHECK_ERR_SIZE,        /**< Image size exceeds partition boundary. */
    FW_CHECK_ERR_PARAM_NULL,  /**< NULL pointer argument. */
    FW_CHECK_ERR_NOT_INIT     /**< Subsystem not initialised. */
} FirmwareCheckResult_t;

/* ---------- API ---------- */

/**
 * Initialise the firmware integrity subsystem.
 *
 * @return true on success.
 */
bool firmware_integrity_init(void);

/**
 * Set the HMAC key used for firmware verification.
 * In production, this key is provisioned at factory time and
 * stored in secure NVM / eFuse. For testing, this allows
 * injection of a known key.
 *
 * @param key       32-byte HMAC key.
 * @param key_len   Must be FW_INTEGRITY_HMAC_SIZE (32).
 * @return true on success.
 */
bool firmware_integrity_set_key(const uint8_t *key, size_t key_len);

/**
 * Verify a firmware image's integrity.
 *
 * @param header      Pointer to the image header (at the start of the partition).
 * @param image_data  Pointer to the firmware image bytes (after the header).
 * @param image_data_len  Length of image_data in bytes.
 * @return FW_CHECK_OK if the HMAC matches.
 */
FirmwareCheckResult_t firmware_integrity_verify(
    const FirmwareImageHeader_t *header,
    const uint8_t *image_data,
    size_t image_data_len
);

/**
 * Compute the HMAC-SHA256 for a firmware image.
 * Used during the build process to stamp the header.
 *
 * @param key           HMAC key.
 * @param key_len       Key length (must be 32).
 * @param image_data    Firmware image bytes.
 * @param image_data_len Length of image data.
 * @param out_hmac      Output: 32-byte HMAC-SHA256 digest.
 */
void firmware_integrity_compute_hmac(
    const uint8_t *key, size_t key_len,
    const uint8_t *image_data, size_t image_data_len,
    uint8_t out_hmac[FW_INTEGRITY_HMAC_SIZE]
);

#endif /* FIRMWARE_INTEGRITY_H */
