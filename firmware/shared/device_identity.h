/**
 * @file device_identity.h
 * @brief Device identity — factory-provisioned, immutable device metadata.
 *
 * Architecture ref: Section 5 (OTA: device identity for image targeting).
 *
 * Each device has an immutable identity set once at factory time:
 * - device_uuid: 16-byte unique identifier (reuses attestation concept)
 * - domain_profile_id: which domain profile this device runs
 * - hw_variant_id: hardware variant for this device
 * - fw_version: currently running firmware version
 *
 * After factory provisioning, device_identity_set() is rejected —
 * the identity is read-only. This prevents OTA or field misconfiguration
 * from changing a device's domain or hardware variant.
 */

#ifndef DEVICE_IDENTITY_H
#define DEVICE_IDENTITY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---------- Constants ---------- */

#define DEVICE_UUID_SIZE  16   /**< Device UUID length in bytes. */
#define DEVICE_ID_NO_VERSION  0   /**< Sentinel: no pending version. */

/* ---------- Types ---------- */

/**
 * Domain profile identifiers.
 * Must match the PROFILE_* preprocessor defines used in build targets.
 * Stored as a runtime enum for OTA manifest matching — the build-time
 * define determines which profile is compiled in, this enum identifies
 * which profile the device IS.
 */
typedef enum {
    DOMAIN_PROFILE_HOME           = 1,
    DOMAIN_PROFILE_AGRICULTURE    = 2,
    DOMAIN_PROFILE_HVAC           = 3,
    DOMAIN_PROFILE_WATER_TREATMENT = 4
} DomainProfileId_t;

/**
 * Device identity — factory-provisioned, immutable after init.
 */
typedef struct {
    uint8_t        device_uuid[DEVICE_UUID_SIZE]; /**< Unique device identifier. */
    DomainProfileId_t domain_profile_id;           /**< Domain profile this device runs. */
    uint8_t        hw_variant_id;                  /**< Hardware variant. */
    uint32_t       fw_version;                     /**< Currently running firmware version. */
} DeviceIdentity_t;

/**
 * Result of device identity operations.
 */
typedef enum {
    DEVICE_ID_OK,
    DEVICE_ID_ERR_ALREADY_PROVISIONED,  /**< Identity already set — cannot mutate. */
    DEVICE_ID_ERR_NOT_PROVISIONED,      /**< Identity not yet set. */
    DEVICE_ID_ERR_PARAM_NULL            /**< NULL pointer argument. */
} DeviceIdentityResult_t;

/**
 * Storage backend for device identity persistence.
 * In production, these callbacks would use NVS or secure NVM.
 * For testing, a RAM-backed implementation is provided.
 */
typedef struct {
    /** Save device identity + pending version to persistent storage. */
    bool (*save)(const DeviceIdentity_t *identity, bool provisioned, uint32_t fw_version_pending);
    /** Load device identity + pending version from persistent storage. */
    bool (*load)(DeviceIdentity_t *identity, bool *provisioned, uint32_t *fw_version_pending);
} DeviceIdentityStorage_t;

/* ---------- API ---------- */

/**
 * Register a storage backend for device identity persistence.
 * Must be called before device_identity_init() if persistence is desired.
 *
 * @param storage  Storage backend callbacks. Pass NULL to disable persistence.
 */
void device_identity_set_storage(const DeviceIdentityStorage_t *storage);

/**
 * Initialise the device identity subsystem.
 * If a storage backend is registered and contains a persisted identity,
 * it is loaded automatically and subsequent device_identity_set() calls
 * are rejected (cross-reboot immutability).
 *
 * @return true on success.
 */
bool device_identity_init(void);

/**
 * Set the device identity (factory provisioning).
 * May only be called ONCE after init. Subsequent calls are rejected
 * with DEVICE_ID_ERR_ALREADY_PROVISIONED — the identity is immutable.
 *
 * @param identity  The device identity to provision (copied internally).
 * @return DEVICE_ID_OK on success, DEVICE_ID_ERR_ALREADY_PROVISIONED
 *         if already provisioned.
 */
DeviceIdentityResult_t device_identity_set(const DeviceIdentity_t *identity);

/**
 * Get the current device identity.
 *
 * @param out_identity  Output: the device identity.
 * @return DEVICE_ID_OK on success, DEVICE_ID_ERR_NOT_PROVISIONED
 *         if not yet provisioned.
 */
DeviceIdentityResult_t device_identity_get(DeviceIdentity_t *out_identity);

/**
 * Get the device's domain profile ID.
 *
 * @return Domain profile ID, or 0 if not provisioned.
 */
DomainProfileId_t device_identity_get_domain(void);

/**
 * Get the device's hardware variant ID.
 *
 * @return Hardware variant ID, or 0 if not provisioned.
 */
uint8_t device_identity_get_hw_variant(void);

/**
 * Get the device's current firmware version.
 *
 * @return Firmware version, or 0 if not provisioned.
 */
uint32_t device_identity_get_fw_version(void);

/**
 * Set a pending firmware version (called at OTA flash time).
 * Does NOT promote the confirmed version — the device is now in
 * an "unconfirmed update" state until confirm_boot() is called.
 *
 * @param new_version  The new firmware version being flashed.
 * @return DEVICE_ID_OK on success.
 */
DeviceIdentityResult_t device_identity_set_pending_version(uint32_t new_version);

/**
 * Confirm that the current firmware boot is successful.
 * Promotes fw_version_pending to fw_version and persists.
 * Must be called after the new firmware has booted and passed self-check.
 *
 * @return DEVICE_ID_OK on success.
 */
DeviceIdentityResult_t device_identity_confirm_boot(void);

/**
 * Get the pending (unconfirmed) firmware version.
 *
 * @return Pending version, or DEVICE_ID_NO_VERSION if none.
 */
uint32_t device_identity_get_pending_version(void);

/**
 * Check if there is an unconfirmed firmware update pending.
 *
 * @return true if a pending version exists.
 */
bool device_identity_has_pending_version(void);

/**
 * Check if the device identity has been provisioned.
 *
 * @return true if provisioned.
 */
bool device_identity_is_provisioned(void);

#endif /* DEVICE_IDENTITY_H */
