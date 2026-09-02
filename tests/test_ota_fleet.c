/**
 * @file test_ota_fleet.c
 * @brief Tests for OTA fleet orchestration — device identity, manifest
 *        compatibility, and canary rollout.
 *
 * Architecture ref: Section 5 (OTA: canary rollout, image targeting).
 *
 * Key test: full 4×4 cross-domain rejection matrix — every domain
 * combination is tested, not just a sample.
 */

#include "test_helpers/test_utils.h"
#include "../firmware/shared/device_identity.h"
#include "../firmware/shared/ota_manifest.h"
#include "../firmware/shared/ota_canary.h"
#include <string.h>

/* ---------- Helpers ---------- */

/** Create a DeviceIdentity_t for a given domain/variant/version. */
static DeviceIdentity_t make_identity(DomainProfileId_t domain,
                                       uint8_t variant, uint32_t version,
                                       uint8_t uuid_suffix)
{
    DeviceIdentity_t id;
    memset(&id, 0, sizeof(id));
    id.device_uuid[0] = uuid_suffix;
    id.device_uuid[1] = (uint8_t)(domain);
    id.device_uuid[2] = variant;
    id.domain_profile_id = domain;
    id.hw_variant_id = variant;
    id.fw_version = version;
    return id;
}

/** Create a FirmwareManifest_t for a given domain/variant/version. */
static FirmwareManifest_t make_manifest(DomainProfileId_t domain,
                                         uint8_t variant, uint32_t version)
{
    FirmwareManifest_t m;
    memset(&m, 0, sizeof(m));
    m.target_domain_profile_id = domain;
    m.target_hw_variant_id = variant;
    m.fw_version = version;
    return m;
}

/* ================================================================
 * DEVICE IDENTITY TESTS
 * ================================================================ */

/* ---------- Test: device identity init ---------- */
static int test_device_identity_init(void)
{
    TEST_ASSERT(device_identity_init() == true);
    TEST_ASSERT(device_identity_is_provisioned() == false);
    TEST_ASSERT(device_identity_get_domain() == (DomainProfileId_t)0);
    TEST_ASSERT(device_identity_get_hw_variant() == 0);
    TEST_ASSERT(device_identity_get_fw_version() == 0);
    TEST_PASS();
}

/* ---------- Test: device identity set ---------- */
static int test_device_identity_set(void)
{
    device_identity_init();
    DeviceIdentity_t id = make_identity(DOMAIN_PROFILE_HOME, 1, 100, 0x01);
    TEST_ASSERT(device_identity_set(&id) == DEVICE_ID_OK);
    TEST_ASSERT(device_identity_is_provisioned() == true);
    TEST_ASSERT(device_identity_get_domain() == DOMAIN_PROFILE_HOME);
    TEST_ASSERT(device_identity_get_hw_variant() == 1);
    TEST_ASSERT(device_identity_get_fw_version() == 100);
    TEST_PASS();
}

/* ---------- Test: device identity immutability ---------- */
static int test_device_identity_immutability(void)
{
    device_identity_init();
    DeviceIdentity_t id1 = make_identity(DOMAIN_PROFILE_HOME, 1, 100, 0x01);
    TEST_ASSERT(device_identity_set(&id1) == DEVICE_ID_OK);

    /* Second set must be rejected. */
    DeviceIdentity_t id2 = make_identity(DOMAIN_PROFILE_AGRICULTURE, 2, 200, 0x02);
    TEST_ASSERT(device_identity_set(&id2) == DEVICE_ID_ERR_ALREADY_PROVISIONED);

    /* Identity must be unchanged. */
    TEST_ASSERT(device_identity_get_domain() == DOMAIN_PROFILE_HOME);
    TEST_ASSERT(device_identity_get_hw_variant() == 1);
    TEST_ASSERT(device_identity_get_fw_version() == 100);
    TEST_PASS();
}

/* ---------- Test: device identity fw_version via pending+confirm ---------- */
static int test_device_identity_fw_update(void)
{
    device_identity_init();
    DeviceIdentity_t id = make_identity(DOMAIN_PROFILE_HOME, 1, 100, 0x01);
    device_identity_set(&id);

    /* fw_version is updated via set_pending + confirm_boot. */
    TEST_ASSERT(device_identity_set_pending_version(200) == DEVICE_ID_OK);
    TEST_ASSERT(device_identity_confirm_boot() == DEVICE_ID_OK);
    TEST_ASSERT(device_identity_get_fw_version() == 200);

    /* Domain and variant must be unchanged. */
    TEST_ASSERT(device_identity_get_domain() == DOMAIN_PROFILE_HOME);
    TEST_ASSERT(device_identity_get_hw_variant() == 1);
    TEST_PASS();
}

/* ---------- Test: device identity not provisioned ---------- */
static int test_device_identity_not_provisioned(void)
{
    device_identity_init();
    DeviceIdentity_t id;
    TEST_ASSERT(device_identity_get(&id) == DEVICE_ID_ERR_NOT_PROVISIONED);
    TEST_ASSERT(device_identity_set_pending_version(1) == DEVICE_ID_ERR_NOT_PROVISIONED);
    TEST_ASSERT(device_identity_confirm_boot() == DEVICE_ID_ERR_NOT_PROVISIONED);
    TEST_PASS();
}

/* ---------- Test: device identity null params ---------- */
static int test_device_identity_null_params(void)
{
    device_identity_init();
    TEST_ASSERT(device_identity_set(NULL) == DEVICE_ID_ERR_PARAM_NULL);
    TEST_ASSERT(device_identity_get(NULL) == DEVICE_ID_ERR_PARAM_NULL);
    TEST_PASS();
}

/* ================================================================
 * DEVICE IDENTITY PERSISTENCE TESTS
 * ================================================================ */

/* RAM-backed storage for device identity persistence tests. */
static DeviceIdentity_t s_persist_id;
static bool s_persist_provisioned = false;
static uint32_t s_persist_pending = DEVICE_ID_NO_VERSION;
static bool s_persist_dirty = false;

static bool di_test_storage_save(const DeviceIdentity_t *identity, bool provisioned, uint32_t fw_version_pending)
{
    memcpy(&s_persist_id, identity, sizeof(DeviceIdentity_t));
    s_persist_provisioned = provisioned;
    s_persist_pending = fw_version_pending;
    s_persist_dirty = true;
    return true;
}

static bool di_test_storage_load(DeviceIdentity_t *identity, bool *provisioned, uint32_t *fw_version_pending)
{
    memcpy(identity, &s_persist_id, sizeof(DeviceIdentity_t));
    *provisioned = s_persist_provisioned;
    *fw_version_pending = s_persist_pending;
    return true;
}

static const DeviceIdentityStorage_t s_di_test_storage = {
    .save = di_test_storage_save,
    .load = di_test_storage_load
};

static void di_simulate_reboot(void)
{
    s_persist_dirty = false;
    device_identity_set_storage(&s_di_test_storage);
    device_identity_init();
}

/** Helper: provision + set storage for persistence tests. */
static void di_test_provision(DomainProfileId_t domain, uint8_t variant, uint32_t version, uint8_t uuid_suffix)
{
    memset(&s_persist_id, 0, sizeof(s_persist_id));
    s_persist_provisioned = false;
    s_persist_pending = DEVICE_ID_NO_VERSION;
    device_identity_set_storage(&s_di_test_storage);
    device_identity_init();
    DeviceIdentity_t id = make_identity(domain, variant, version, uuid_suffix);
    device_identity_set(&id);
}

/* ---------- Test: device identity survives reboot ---------- */
static int test_device_identity_survives_reboot(void)
{
    /* Factory provisioning. */
    memset(&s_persist_id, 0, sizeof(s_persist_id));
    s_persist_provisioned = false;
    device_identity_set_storage(&s_di_test_storage);
    device_identity_init();

    DeviceIdentity_t id = make_identity(DOMAIN_PROFILE_WATER_TREATMENT, 3, 100, 0x42);
    TEST_ASSERT(device_identity_set(&id) == DEVICE_ID_OK);
    TEST_ASSERT(s_persist_dirty == true);

    /* Simulate reboot. */
    di_simulate_reboot();

    /* Identity must have survived. */
    TEST_ASSERT(device_identity_is_provisioned() == true);
    DeviceIdentity_t loaded;
    TEST_ASSERT(device_identity_get(&loaded) == DEVICE_ID_OK);
    TEST_ASSERT(loaded.domain_profile_id == DOMAIN_PROFILE_WATER_TREATMENT);
    TEST_ASSERT(loaded.hw_variant_id == 3);
    TEST_ASSERT(loaded.fw_version == 100);
    TEST_ASSERT(memcmp(loaded.device_uuid, id.device_uuid, DEVICE_UUID_SIZE) == 0);
    TEST_PASS();
}

/* ---------- Test: no reprovisioning after reboot ---------- */
static int test_device_identity_no_reprovisioning_after_reboot(void)
{
    /* Provision as Home. */
    memset(&s_persist_id, 0, sizeof(s_persist_id));
    s_persist_provisioned = false;
    device_identity_set_storage(&s_di_test_storage);
    device_identity_init();

    DeviceIdentity_t home_id = make_identity(DOMAIN_PROFILE_HOME, 1, 50, 0x01);
    TEST_ASSERT(device_identity_set(&home_id) == DEVICE_ID_OK);

    /* Reboot. */
    di_simulate_reboot();

    /* Attempt to re-provision as Water Treatment — must be rejected. */
    DeviceIdentity_t water_id = make_identity(DOMAIN_PROFILE_WATER_TREATMENT, 2, 99, 0x99);
    TEST_ASSERT(device_identity_set(&water_id) == DEVICE_ID_ERR_ALREADY_PROVISIONED);

    /* Identity must still be Home. */
    TEST_ASSERT(device_identity_get_domain() == DOMAIN_PROFILE_HOME);
    TEST_ASSERT(device_identity_get_hw_variant() == 1);

    /* OTA check: Home device rejects Water Treatment manifest. */
    DeviceIdentity_t dev;
    device_identity_get(&dev);
    FirmwareManifest_t manifest = make_manifest(DOMAIN_PROFILE_WATER_TREATMENT, 2, 200);
    TEST_ASSERT(ota_compatibility_check(&dev, &manifest) == OTA_COMPAT_ERR_DOMAIN_MISMATCH);
    TEST_PASS();
}

/* ---------- Test: fw_version update persists across reboot ---------- */
static int test_device_identity_fw_update_persists(void)
{
    memset(&s_persist_id, 0, sizeof(s_persist_id));
    s_persist_provisioned = false;
    device_identity_set_storage(&s_di_test_storage);
    device_identity_init();

    DeviceIdentity_t id = make_identity(DOMAIN_PROFILE_HVAC, 1, 100, 0x05);
    device_identity_set(&id);

    /* OTA update bumps version via pending+confirm. */
    device_identity_set_pending_version(200);
    device_identity_confirm_boot();

    /* Reboot. */
    di_simulate_reboot();

    /* Version must be the updated one. */
    TEST_ASSERT_EQUAL(200, device_identity_get_fw_version());
    TEST_ASSERT(device_identity_get_domain() == DOMAIN_PROFILE_HVAC);
    TEST_PASS();
}

/* ---------- Test: fw_version rollback scenario ---------- */
static int test_fw_version_rollback_scenario(void)
{
    /* Provision as Home v100. */
    di_test_provision(DOMAIN_PROFILE_HOME, 1, 100, 0x01);
    TEST_ASSERT_EQUAL(100, device_identity_get_fw_version());

    /* OTA flash: set pending v200 (simulating new image written). */
    TEST_ASSERT(device_identity_set_pending_version(200) == DEVICE_ID_OK);
    TEST_ASSERT(device_identity_has_pending_version() == true);
    TEST_ASSERT_EQUAL(200, device_identity_get_pending_version());
    /* Confirmed version is still 100. */
    TEST_ASSERT_EQUAL(100, device_identity_get_fw_version());

    /* Simulate reboot WITHOUT calling confirm_boot() (rollback scenario).
     * The new firmware failed to boot or was rolled back by canary logic.
     * On init, the pending version is discarded. */
    di_simulate_reboot();

    /* Confirmed version must still be 100. */
    TEST_ASSERT_EQUAL(100, device_identity_get_fw_version());
    TEST_ASSERT(device_identity_has_pending_version() == false);

    /* A manifest offering v200 must be ACCEPTED (not rejected as downgrade). */
    DeviceIdentity_t dev;
    device_identity_get(&dev);
    FirmwareManifest_t manifest = make_manifest(DOMAIN_PROFILE_HOME, 1, 200);
    TEST_ASSERT(ota_compatibility_check(&dev, &manifest) == OTA_COMPAT_OK);
    TEST_PASS();
}

/* ---------- Test: fw_version confirm_boot success ---------- */
static int test_fw_version_confirm_boot_success(void)
{
    /* Provision as HVAC v100. */
    di_test_provision(DOMAIN_PROFILE_HVAC, 1, 100, 0x05);

    /* OTA flash: set pending v200. */
    device_identity_set_pending_version(200);
    TEST_ASSERT_EQUAL(100, device_identity_get_fw_version());
    TEST_ASSERT_EQUAL(200, device_identity_get_pending_version());

    /* New firmware boots successfully — call confirm_boot(). */
    TEST_ASSERT(device_identity_confirm_boot() == DEVICE_ID_OK);

    /* Version promoted. */
    TEST_ASSERT_EQUAL(200, device_identity_get_fw_version());
    TEST_ASSERT(device_identity_has_pending_version() == false);

    /* Persist across reboot. */
    di_simulate_reboot();
    TEST_ASSERT_EQUAL(200, device_identity_get_fw_version());
    TEST_ASSERT(device_identity_has_pending_version() == false);

    /* Manifest for v200 now rejected (not greater than confirmed). */
    DeviceIdentity_t dev;
    device_identity_get(&dev);
    FirmwareManifest_t m200 = make_manifest(DOMAIN_PROFILE_HVAC, 1, 200);
    TEST_ASSERT(ota_compatibility_check(&dev, &m200) == OTA_COMPAT_ERR_VERSION_DOWNGRADE);

    /* Manifest for v300 accepted. */
    FirmwareManifest_t m300 = make_manifest(DOMAIN_PROFILE_HVAC, 1, 300);
    TEST_ASSERT(ota_compatibility_check(&dev, &m300) == OTA_COMPAT_OK);
    TEST_PASS();
}

/* ---------- Test: no storage backward compatibility ---------- */
static int test_device_identity_no_storage_no_persistence(void)
{
    /* No storage backend — same as original behavior. */
    device_identity_set_storage(NULL);
    device_identity_init();

    DeviceIdentity_t id = make_identity(DOMAIN_PROFILE_HOME, 1, 100, 0x01);
    TEST_ASSERT(device_identity_set(&id) == DEVICE_ID_OK);
    TEST_ASSERT(device_identity_is_provisioned() == true);

    /* Reinit (simulate reboot without storage). */
    device_identity_init();

    /* Identity lost — no persistence. */
    TEST_ASSERT(device_identity_is_provisioned() == false);

    /* Can re-provision. */
    TEST_ASSERT(device_identity_set(&id) == DEVICE_ID_OK);
    TEST_PASS();
}

/* ================================================================
 * OTA MANIFEST COMPATIBILITY TESTS
 * ================================================================ */

/* ---------- Test: compatible manifest accepted ---------- */
static int test_ota_compatible_manifest(void)
{
    DeviceIdentity_t dev = make_identity(DOMAIN_PROFILE_HOME, 1, 100, 0x01);
    FirmwareManifest_t manifest = make_manifest(DOMAIN_PROFILE_HOME, 1, 200);
    TEST_ASSERT(ota_compatibility_check(&dev, &manifest) == OTA_COMPAT_OK);
    TEST_PASS();
}

/* ---------- Test: domain mismatch rejected ---------- */
static int test_ota_domain_mismatch(void)
{
    DeviceIdentity_t dev = make_identity(DOMAIN_PROFILE_HOME, 1, 100, 0x01);
    FirmwareManifest_t manifest = make_manifest(DOMAIN_PROFILE_AGRICULTURE, 1, 200);
    TEST_ASSERT(ota_compatibility_check(&dev, &manifest) == OTA_COMPAT_ERR_DOMAIN_MISMATCH);
    TEST_PASS();
}

/* ---------- Test: variant mismatch rejected ---------- */
static int test_ota_variant_mismatch(void)
{
    DeviceIdentity_t dev = make_identity(DOMAIN_PROFILE_HOME, 1, 100, 0x01);
    FirmwareManifest_t manifest = make_manifest(DOMAIN_PROFILE_HOME, 2, 200);
    TEST_ASSERT(ota_compatibility_check(&dev, &manifest) == OTA_COMPAT_ERR_VARIANT_MISMATCH);
    TEST_PASS();
}

/* ---------- Test: version downgrade rejected ---------- */
static int test_ota_version_downgrade(void)
{
    DeviceIdentity_t dev = make_identity(DOMAIN_PROFILE_HOME, 1, 200, 0x01);
    FirmwareManifest_t manifest = make_manifest(DOMAIN_PROFILE_HOME, 1, 100);
    TEST_ASSERT(ota_compatibility_check(&dev, &manifest) == OTA_COMPAT_ERR_VERSION_DOWNGRADE);
    TEST_PASS();
}

/* ---------- Test: same version rejected (not strictly greater) ---------- */
static int test_ota_same_version(void)
{
    DeviceIdentity_t dev = make_identity(DOMAIN_PROFILE_HOME, 1, 100, 0x01);
    FirmwareManifest_t manifest = make_manifest(DOMAIN_PROFILE_HOME, 1, 100);
    TEST_ASSERT(ota_compatibility_check(&dev, &manifest) == OTA_COMPAT_ERR_VERSION_DOWNGRADE);
    TEST_PASS();
}

/* ---------- Test: null params ---------- */
static int test_ota_manifest_null_params(void)
{
    DeviceIdentity_t dev = make_identity(DOMAIN_PROFILE_HOME, 1, 100, 0x01);
    FirmwareManifest_t manifest = make_manifest(DOMAIN_PROFILE_HOME, 1, 200);
    TEST_ASSERT(ota_compatibility_check(NULL, &manifest) == OTA_COMPAT_ERR_PARAM_NULL);
    TEST_ASSERT(ota_compatibility_check(&dev, NULL) == OTA_COMPAT_ERR_PARAM_NULL);
    TEST_PASS();
}

/* ---------- Test: result string ---------- */
static int test_ota_compat_result_str(void)
{
    TEST_ASSERT(strcmp(ota_compat_result_str(OTA_COMPAT_OK), "OK — compatible") == 0);
    TEST_ASSERT(strcmp(ota_compat_result_str(OTA_COMPAT_ERR_DOMAIN_MISMATCH),
                       "REJECT — domain profile mismatch") == 0);
    TEST_PASS();
}

/* ================================================================
 * FULL 4×4 CROSS-DOMAIN REJECTION MATRIX
 *
 * This is the most important test in this phase.
 * Every (device_domain × manifest_domain) combination is tested.
 * ================================================================ */

static int test_ota_full_4x4_domain_matrix(void)
{
    DomainProfileId_t domains[4] = {
        DOMAIN_PROFILE_HOME,
        DOMAIN_PROFILE_AGRICULTURE,
        DOMAIN_PROFILE_HVAC,
        DOMAIN_PROFILE_WATER_TREATMENT
    };

    /* For each device domain × manifest domain combination: */
    for (int d = 0; d < 4; d++) {
        for (int m = 0; m < 4; m++) {
            DeviceIdentity_t dev = make_identity(domains[d], 1, 100, 0x01);
            FirmwareManifest_t manifest = make_manifest(domains[m], 1, 200);

            OtaCompatResult_t result = ota_compatibility_check(&dev, &manifest);

            if (d == m) {
                /* Same domain: must be accepted (assuming same variant). */
                TEST_ASSERT(result == OTA_COMPAT_OK);
            } else {
                /* Different domain: must be rejected. */
                TEST_ASSERT(result == OTA_COMPAT_ERR_DOMAIN_MISMATCH);
            }
        }
    }

    /* Also test cross-variant: same domain, different variant → rejected. */
    for (int d = 0; d < 4; d++) {
        DeviceIdentity_t dev = make_identity(domains[d], 1, 100, 0x01);
        FirmwareManifest_t manifest = make_manifest(domains[d], 2, 200);
        TEST_ASSERT(ota_compatibility_check(&dev, &manifest) == OTA_COMPAT_ERR_VARIANT_MISMATCH);
    }

    /* Also test cross-variant AND cross-domain simultaneously: rejected. */
    for (int d = 0; d < 4; d++) {
        for (int m = 0; m < 4; m++) {
            if (d == m) continue;  /* Skip same-domain (already tested above). */
            DeviceIdentity_t dev = make_identity(domains[d], 1, 100, 0x01);
            FirmwareManifest_t manifest = make_manifest(domains[m], 2, 200);
            /* Domain mismatch takes priority in check order. */
            TEST_ASSERT(ota_compatibility_check(&dev, &manifest) == OTA_COMPAT_ERR_DOMAIN_MISMATCH);
        }
    }

    TEST_PASS();
}

/* ================================================================
 * CANARY ROLLOUT TESTS
 * ================================================================ */

/* ---------- Test: canary init ---------- */
static int test_canary_init(void)
{
    TEST_ASSERT(ota_canary_init() == true);
    TEST_ASSERT_EQUAL(0, ota_canary_get_group_count());
    TEST_PASS();
}

/* ---------- Test: canary create group ---------- */
static int test_canary_create_group(void)
{
    ota_canary_init();
    OtaCanaryGroup_t *group = NULL;
    TEST_ASSERT(ota_canary_create_group(DOMAIN_PROFILE_HOME, 1, 10, 50, &group) == OTA_CANARY_OK);
    TEST_ASSERT_NOT_NULL(group);
    TEST_ASSERT(group->domain_profile_id == DOMAIN_PROFILE_HOME);
    TEST_ASSERT(group->hw_variant_id == 1);
    TEST_ASSERT_EQUAL(10, group->canary_percentage);
    TEST_ASSERT_EQUAL(50, group->error_rate_threshold);
    TEST_ASSERT_EQUAL(1, ota_canary_get_group_count());
    TEST_PASS();
}

/* ---------- Test: canary get existing group ---------- */
static int test_canary_get_existing_group(void)
{
    ota_canary_init();
    OtaCanaryGroup_t *g1 = NULL, *g2 = NULL;
    ota_canary_create_group(DOMAIN_PROFILE_HOME, 1, 10, 50, &g1);
    TEST_ASSERT(ota_canary_get_group(DOMAIN_PROFILE_HOME, 1, &g2) == OTA_CANARY_OK);
    TEST_ASSERT(g1 == g2);  /* Same pointer — not a copy. */
    TEST_PASS();
}

/* ---------- Test: canary deterministic bucketing ---------- */
static int test_canary_deterministic_bucketing(void)
{
    ota_canary_init();
    OtaCanaryGroup_t *group = NULL;
    ota_canary_create_group(DOMAIN_PROFILE_HOME, 1, 50, 50, &group);

    /* Same UUID must always produce the same bucket. */
    uint8_t uuid[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                         0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};

    bool result1 = ota_canary_is_in_canary(uuid, group);
    bool result2 = ota_canary_is_in_canary(uuid, group);
    TEST_ASSERT(result1 == result2);

    /* Different UUIDs may or may not be in canary (deterministic, not random). */
    uint8_t uuid2[16];
    memcpy(uuid2, uuid, 16);
    uuid2[0] = 0xFF;  /* Different UUID. */
    bool result3 = ota_canary_is_in_canary(uuid2, group);
    (void)result3;  /* May be true or false — just verify no crash. */

    TEST_PASS();
}

/* ---------- Test: canary 0% means nobody in canary ---------- */
static int test_canary_zero_percent(void)
{
    ota_canary_init();
    OtaCanaryGroup_t *group = NULL;
    ota_canary_create_group(DOMAIN_PROFILE_HOME, 1, 0, 50, &group);

    uint8_t uuid[16] = {0x01};
    TEST_ASSERT(ota_canary_is_in_canary(uuid, group) == false);
    TEST_PASS();
}

/* ---------- Test: canary 100% means everybody in canary ---------- */
static int test_canary_hundred_percent(void)
{
    ota_canary_init();
    OtaCanaryGroup_t *group = NULL;
    ota_canary_create_group(DOMAIN_PROFILE_HOME, 1, 100, 50, &group);

    uint8_t uuid[16] = {0x01};
    TEST_ASSERT(ota_canary_is_in_canary(uuid, group) == true);
    TEST_PASS();
}

/* ---------- Test: canary error threshold triggers rollback ---------- */
static int test_canary_error_threshold_rollback(void)
{
    ota_canary_init();
    OtaCanaryGroup_t *group = NULL;
    ota_canary_create_group(DOMAIN_PROFILE_HOME, 1, 50, 50, &group);

    /* 1 success, then 2 failures: error rate exceeds 50% threshold. */
    TEST_ASSERT(ota_canary_record_attempt(group, true) == true);   /* 1/1 = 0% */
    TEST_ASSERT(ota_canary_record_attempt(group, false) == true);  /* 1/2 = 50% — at threshold */
    TEST_ASSERT(ota_canary_record_attempt(group, false) == false); /* 2/3 = 66% > 50% → rollback */

    TEST_ASSERT(group->rollback_triggered == true);
    TEST_ASSERT_EQUAL(2, group->error_count);
    TEST_ASSERT_EQUAL(3, group->total_attempts);
    TEST_PASS();
}

/* ---------- Test: canary rollback below threshold ---------- */
static int test_canary_no_rollback_below_threshold(void)
{
    ota_canary_init();
    OtaCanaryGroup_t *group = NULL;
    ota_canary_create_group(DOMAIN_PROFILE_HOME, 1, 50, 50, &group);

    /* 1 failure out of 4 attempts = 25% < 50% threshold → no rollback. */
    TEST_ASSERT(ota_canary_record_attempt(group, true) == true);
    TEST_ASSERT(ota_canary_record_attempt(group, true) == true);
    TEST_ASSERT(ota_canary_record_attempt(group, false) == true);
    TEST_ASSERT(ota_canary_record_attempt(group, true) == true);

    TEST_ASSERT(group->rollback_triggered == false);
    TEST_PASS();
}

/* ---------- Test: canary manual rollback ---------- */
static int test_canary_manual_rollback(void)
{
    ota_canary_init();
    OtaCanaryGroup_t *group = NULL;
    ota_canary_create_group(DOMAIN_PROFILE_HOME, 1, 50, 50, &group);

    TEST_ASSERT(ota_canary_rollback(group) == OTA_CANARY_OK);
    TEST_ASSERT(group->rollback_triggered == true);
    TEST_PASS();
}

/* ================================================================
 * CANARY ROLLBACK ISOLATION — THE CRITICAL TEST
 *
 * A rollback in one domain's group must NOT change rollout state
 * for any other domain's group.
 * ================================================================ */

static int test_canary_rollback_isolation(void)
{
    ota_canary_init();

    /* Create 4 independent canary groups (one per domain). */
    OtaCanaryGroup_t *home = NULL, *agri = NULL, *hvac = NULL, *water = NULL;
    ota_canary_create_group(DOMAIN_PROFILE_HOME, 1, 50, 50, &home);
    ota_canary_create_group(DOMAIN_PROFILE_AGRICULTURE, 1, 50, 50, &agri);
    ota_canary_create_group(DOMAIN_PROFILE_HVAC, 1, 50, 50, &hvac);
    ota_canary_create_group(DOMAIN_PROFILE_WATER_TREATMENT, 1, 50, 50, &water);

    /* Record successful attempts for all groups. */
    for (int i = 0; i < 10; i++) {
        ota_canary_record_attempt(home, true);
        ota_canary_record_attempt(agri, true);
        ota_canary_record_attempt(hvac, true);
        ota_canary_record_attempt(water, true);
    }

    /* Trigger rollback ONLY for Water Treatment. */
    ota_canary_rollback(water);

    /* Verify: Water Treatment is rolled back. */
    TEST_ASSERT(water->rollback_triggered == true);

    /* Verify: all other groups are UNCHANGED. */
    TEST_ASSERT(home->rollback_triggered == false);
    TEST_ASSERT(agri->rollback_triggered == false);
    TEST_ASSERT(hvac->rollback_triggered == false);

    /* Verify: their stats are intact. */
    TEST_ASSERT_EQUAL(10, home->devices_rolled_out);
    TEST_ASSERT_EQUAL(10, agri->devices_rolled_out);
    TEST_ASSERT_EQUAL(10, hvac->devices_rolled_out);
    TEST_ASSERT_EQUAL(10, water->devices_rolled_out);  /* Still counted, just rolled back. */

    /* Verify: auto-rollback in Agriculture doesn't affect others. */
    /* Make Agriculture exceed its threshold. */
    agri->error_rate_threshold = 10;  /* Low threshold for testing. */
    for (int i = 0; i < 5; i++) {
        ota_canary_record_attempt(agri, false);  /* All failures. */
    }
    TEST_ASSERT(agri->rollback_triggered == true);

    /* Others still unaffected. */
    TEST_ASSERT(home->rollback_triggered == false);
    TEST_ASSERT(hvac->rollback_triggered == false);
    TEST_ASSERT(water->rollback_triggered == true);  /* Still from manual rollback. */

    TEST_PASS();
}

/* ---------- Test: canary get group not found ---------- */
static int test_canary_get_group_not_found(void)
{
    ota_canary_init();
    OtaCanaryGroup_t *group = NULL;
    TEST_ASSERT(ota_canary_get_group(DOMAIN_PROFILE_HOME, 1, &group) == OTA_CANARY_ERR_GROUP_NOT_FOUND);
    TEST_PASS();
}

/* ---------- Test: canary max groups ---------- */
static int test_canary_max_groups(void)
{
    ota_canary_init();
    OtaCanaryGroup_t *group = NULL;

    /* Fill all 16 group slots. */
    for (int i = 0; i < OTA_CANARY_MAX_GROUPS; i++) {
        TEST_ASSERT(ota_canary_create_group((DomainProfileId_t)(i + 1), 1, 10, 50, &group) == OTA_CANARY_OK);
    }
    TEST_ASSERT_EQUAL(OTA_CANARY_MAX_GROUPS, ota_canary_get_group_count());

    /* 17th group must fail. */
    TEST_ASSERT(ota_canary_create_group((DomainProfileId_t)(OTA_CANARY_MAX_GROUPS + 1), 1, 10, 50, &group) == OTA_CANARY_ERR_GROUP_FULL);
    TEST_PASS();
}

/* ---------- Test: hash bucket determinism ---------- */
static int test_hash_bucket_determinism(void)
{
    uint8_t data1[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t data2[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t data3[] = {0x01, 0x02, 0x03, 0x05};

    uint8_t b1 = ota_canary_hash_bucket(data1, 4);
    uint8_t b2 = ota_canary_hash_bucket(data2, 4);
    uint8_t b3 = ota_canary_hash_bucket(data3, 4);

    TEST_ASSERT_EQUAL(b1, b2);  /* Same input → same bucket. */
    /* b3 may or may not differ — but verify it's in 0-99 range. */
    TEST_ASSERT(b3 < 100);
    TEST_ASSERT(b1 < 100);
    TEST_PASS();
}

/* ================================================================
 * MAIN
 * ================================================================ */

int main(void)
{
    int _total = 0, _passed = 0, _failed = 0;

    printf("=== test_ota_fleet ===\n");

    /* Device identity tests. */
    RUN_TEST(test_device_identity_init);
    RUN_TEST(test_device_identity_set);
    RUN_TEST(test_device_identity_immutability);
    RUN_TEST(test_device_identity_fw_update);
    RUN_TEST(test_device_identity_not_provisioned);
    RUN_TEST(test_device_identity_null_params);
    RUN_TEST(test_device_identity_survives_reboot);
    RUN_TEST(test_device_identity_no_reprovisioning_after_reboot);
    RUN_TEST(test_device_identity_fw_update_persists);
    RUN_TEST(test_fw_version_rollback_scenario);
    RUN_TEST(test_fw_version_confirm_boot_success);
    RUN_TEST(test_device_identity_no_storage_no_persistence);

    /* OTA manifest compatibility tests. */
    RUN_TEST(test_ota_compatible_manifest);
    RUN_TEST(test_ota_domain_mismatch);
    RUN_TEST(test_ota_variant_mismatch);
    RUN_TEST(test_ota_version_downgrade);
    RUN_TEST(test_ota_same_version);
    RUN_TEST(test_ota_manifest_null_params);
    RUN_TEST(test_ota_compat_result_str);

    /* Full 4×4 cross-domain matrix. */
    RUN_TEST(test_ota_full_4x4_domain_matrix);

    /* Canary rollout tests. */
    RUN_TEST(test_canary_init);
    RUN_TEST(test_canary_create_group);
    RUN_TEST(test_canary_get_existing_group);
    RUN_TEST(test_canary_deterministic_bucketing);
    RUN_TEST(test_canary_zero_percent);
    RUN_TEST(test_canary_hundred_percent);
    RUN_TEST(test_canary_error_threshold_rollback);
    RUN_TEST(test_canary_no_rollback_below_threshold);
    RUN_TEST(test_canary_manual_rollback);
    RUN_TEST(test_canary_rollback_isolation);
    RUN_TEST(test_canary_get_group_not_found);
    RUN_TEST(test_canary_max_groups);
    RUN_TEST(test_hash_bucket_determinism);

    PRINT_TEST_SUMMARY();
}
