/**
 * @file device_identity.c
 * @brief Device identity implementation — factory-provisioned, immutable.
 *
 * Architecture ref: Section 5 (OTA: device identity for image targeting).
 *
 * Two-state firmware version model:
 * - fw_version: confirmed, persisted, what ota_compatibility_check() uses
 * - fw_version_pending: set when a new image is flashed but not yet confirmed
 *
 * On boot, if fw_version_pending is set but confirm_boot() was never called
 * (implying a rollback happened), fw_version remains at its last-confirmed
 * value and fw_version_pending is cleared.
 */

#include "device_identity.h"
#include <string.h>

/* ---------- Internal state ---------- */

static DeviceIdentity_t s_identity;
static bool s_provisioned = false;
static bool s_initialised = false;
static uint32_t s_fw_version_pending = DEVICE_ID_NO_VERSION;
static const DeviceIdentityStorage_t *s_storage = NULL;

/* ---------- Internal helpers ---------- */

static void identity_persist(void)
{
    if (s_storage && s_storage->save) {
        s_storage->save(&s_identity, s_provisioned, s_fw_version_pending);
    }
}

/* ---------- Public API ---------- */

void device_identity_set_storage(const DeviceIdentityStorage_t *storage)
{
    s_storage = storage;
}

bool device_identity_init(void)
{
    memset(&s_identity, 0, sizeof(DeviceIdentity_t));
    s_provisioned = false;
    s_fw_version_pending = DEVICE_ID_NO_VERSION;

    /* Load from storage if a backend is registered. */
    if (s_storage && s_storage->load) {
        bool persisted_provisioned = false;
        uint32_t persisted_pending = DEVICE_ID_NO_VERSION;
        if (s_storage->load(&s_identity, &persisted_provisioned, &persisted_pending)) {
            s_provisioned = persisted_provisioned;

            /*
             * Confirm-before-commit logic:
             * If fw_version_pending was set but confirm_boot() was never called
             * before this reboot (implying the new firmware failed to boot or
             * was rolled back), discard the pending version and keep the
             * last-confirmed fw_version.
             *
             * The pending version is only promoted if confirm_boot() is called
             * during this boot cycle.
             */
            if (persisted_pending != DEVICE_ID_NO_VERSION) {
                /* A pending update exists from the previous boot. Since we're
                 * re-initialising (i.e. rebooted), and confirm_boot() was not
                 * called in the previous boot's post-boot window, this means
                 * the update was not confirmed. Discard it. */
                s_fw_version_pending = DEVICE_ID_NO_VERSION;
                /* fw_version stays at its last-confirmed value. */
                identity_persist();  /* Clear pending from storage. */
            }
        }
    }

    s_initialised = true;
    return true;
}

DeviceIdentityResult_t device_identity_set(const DeviceIdentity_t *identity)
{
    if (!s_initialised) return DEVICE_ID_ERR_NOT_PROVISIONED;
    if (!identity) return DEVICE_ID_ERR_PARAM_NULL;
    if (s_provisioned) return DEVICE_ID_ERR_ALREADY_PROVISIONED;

    memcpy(&s_identity, identity, sizeof(DeviceIdentity_t));
    s_provisioned = true;

    /* Persist to storage so identity survives reboots. */
    identity_persist();

    return DEVICE_ID_OK;
}

DeviceIdentityResult_t device_identity_get(DeviceIdentity_t *out_identity)
{
    if (!out_identity) return DEVICE_ID_ERR_PARAM_NULL;
    if (!s_initialised) return DEVICE_ID_ERR_NOT_PROVISIONED;
    if (!s_provisioned) return DEVICE_ID_ERR_NOT_PROVISIONED;

    memcpy(out_identity, &s_identity, sizeof(DeviceIdentity_t));
    return DEVICE_ID_OK;
}

DomainProfileId_t device_identity_get_domain(void)
{
    if (!s_initialised || !s_provisioned) return (DomainProfileId_t)0;
    return s_identity.domain_profile_id;
}

uint8_t device_identity_get_hw_variant(void)
{
    if (!s_initialised || !s_provisioned) return 0;
    return s_identity.hw_variant_id;
}

uint32_t device_identity_get_fw_version(void)
{
    if (!s_initialised || !s_provisioned) return 0;
    return s_identity.fw_version;
}

DeviceIdentityResult_t device_identity_set_pending_version(uint32_t new_version)
{
    if (!s_initialised) return DEVICE_ID_ERR_NOT_PROVISIONED;
    if (!s_provisioned) return DEVICE_ID_ERR_NOT_PROVISIONED;

    s_fw_version_pending = new_version;
    identity_persist();

    return DEVICE_ID_OK;
}

DeviceIdentityResult_t device_identity_confirm_boot(void)
{
    if (!s_initialised) return DEVICE_ID_ERR_NOT_PROVISIONED;
    if (!s_provisioned) return DEVICE_ID_ERR_NOT_PROVISIONED;
    if (s_fw_version_pending == DEVICE_ID_NO_VERSION) return DEVICE_ID_OK;  /* Nothing to confirm. */

    /* Promote pending → confirmed. */
    s_identity.fw_version = s_fw_version_pending;
    s_fw_version_pending = DEVICE_ID_NO_VERSION;

    /* Persist the promoted version and cleared pending state. */
    identity_persist();

    return DEVICE_ID_OK;
}

uint32_t device_identity_get_pending_version(void)
{
    if (!s_initialised || !s_provisioned) return DEVICE_ID_NO_VERSION;
    return s_fw_version_pending;
}

bool device_identity_has_pending_version(void)
{
    return s_initialised && s_provisioned && (s_fw_version_pending != DEVICE_ID_NO_VERSION);
}

bool device_identity_is_provisioned(void)
{
    return s_initialised && s_provisioned;
}
