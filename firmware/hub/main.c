/**
 * @file main.c
 * @brief Hub main entry point — native/host build only.
 *
 * Architecture ref: Section 2 (Tier 3 — Hub).
 *
 * For ESP32 target: see app_main.cpp (Arduino setup/loop).
 * This file is only compiled for native/host test builds.
 */

#include <stdio.h>
#include <string.h>

/* Shared subsystems. */
#include "../shared/attestation.h"
#include "../shared/transport_encryption.h"
#include "../shared/replay_protection.h"
#include "../shared/firmware_integrity.h"
#include "../shared/rule_engine_core.h"
#include "../shared/sensor_validation.h"
#include "../shared/actuator_failsafe.h"

/* Gateway auth + ingestion handler + historian. */
#include "../gateway/gateway_auth.h"
#include "../gateway/batch_forwarder.h"
#include "ingestion_handler.h"
#include "historian.h"

/* Radio handlers (one active per profile). */
#ifdef PROFILE_HOME
#include "../shared/zigbee_handler.h"
#endif
#if defined(PROFILE_AGRICULTURE) || defined(PROFILE_HVAC) || defined(PROFILE_WATER_TREATMENT)
#include "../shared/lora_handler.h"
#endif

/* Domain profiles — conditional compilation. */
#ifdef PROFILE_HOME
#include "../profiles/home/home_rules.h"
#include "../profiles/home/home_validation_bounds.h"
#include "../profiles/home/home_failsafe.h"
#endif
#ifdef PROFILE_AGRICULTURE
#include "../profiles/agriculture/agriculture_rules.h"
#include "../profiles/agriculture/agriculture_validation_bounds.h"
#include "../profiles/agriculture/agriculture_failsafe.h"
#endif
#ifdef PROFILE_HVAC
#include "../profiles/hvac/hvac_rules.h"
#include "../profiles/hvac/hvac_validation_bounds.h"
#include "../profiles/hvac/hvac_failsafe.h"
#endif
#ifdef PROFILE_WATER_TREATMENT
#include "../profiles/water_treatment/water_rules.h"
#include "../profiles/water_treatment/water_validation_bounds.h"
#include "../profiles/water_treatment/water_failsafe.h"
#endif

/* ---------- Hub-level state for ingestion pipeline ---------- */

/** Hub-side Gateway auth tracker (for frame HMAC verification). */
static GatewayGatewayTracker_t s_hub_gw_tracker;

/** Hub-side Edge Node attestation trust tracker. */
static EdgeNodeTracker_t s_hub_edge_tracker;

/* Config portal. */
#include "config_portal.h"

/**
 * Radio adapter: Zigbee receive → RadioReceiveFunc_t.
 *
 * Wraps zigbee_receive() (returns ZigbeeFrame_t) into the raw-byte
 * signature expected by ingestion_handler. Extracts the application
 * payload from the Zigbee APS frame.
 *
 * Used by: Home profile.
 */
#ifdef PROFILE_HOME
static bool zigbee_radio_adapter(uint8_t *out_payload, size_t max_len, size_t *out_len)
{
    if (!out_payload || !out_len) return false;
    ZigbeeFrame_t frame;
    if (zigbee_receive(&frame) != ZIGBEE_OK) return false;
    if (frame.payload_len == 0) return false;
    if (frame.payload_len > max_len) return false;
    memcpy(out_payload, frame.payload, frame.payload_len);
    *out_len = (size_t)frame.payload_len;
    return true;
}
#endif

/**
 * Radio adapter: LoRa receive → RadioReceiveFunc_t.
 *
 * Wraps lora_receive() (returns LoRaFrame_t) into the raw-byte
 * signature expected by ingestion_handler. Extracts the application
 * payload from the LoRa frame.
 *
 * Used by: Agriculture, HVAC, Water Treatment profiles.
 */
#if defined(PROFILE_AGRICULTURE) || defined(PROFILE_HVAC) || defined(PROFILE_WATER_TREATMENT)
static bool lora_radio_adapter(uint8_t *out_payload, size_t max_len, size_t *out_len)
{
    if (!out_payload || !out_len) return false;
    LoRaFrame_t frame;
    if (lora_receive(&frame) != LORA_OK) return false;
    if (frame.payload_len == 0) return false;
    if (frame.payload_len > max_len) return false;
    memcpy(out_payload, frame.payload, frame.payload_len);
    *out_len = (size_t)frame.payload_len;
    return true;
}
#endif

/**
 * Initialise all subsystems for the hub_home build.
 *
 * @return true on success.
 */
bool hub_init(void)
{
    printf("[hub] Initialising subsystems...\n");

    /* Core security. */
    if (!attestation_init()) {
        printf("[hub] FAIL: attestation_init\n");
        return false;
    }
    if (!transport_init()) {
        printf("[hub] FAIL: transport_init\n");
        return false;
    }
    if (!replay_init()) {
        printf("[hub] FAIL: replay_init\n");
        return false;
    }
    if (!firmware_integrity_init()) {
        printf("[hub] FAIL: firmware_integrity_init\n");
        return false;
    }

    /* Rule engine. */
    if (!rule_engine_init()) {
        printf("[hub] FAIL: rule_engine_init\n");
        return false;
    }

    /* Sensor validation. */
    if (!sensor_validation_init()) {
        printf("[hub] FAIL: sensor_validation_init\n");
        return false;
    }

    /* Actuator fail-safe. */
    if (!actuator_failsafe_init()) {
        printf("[hub] FAIL: actuator_failsafe_init\n");
        return false;
    }

    /* Domain profile fail-safe registration. */
#ifdef PROFILE_HOME
    if (!home_failsafe_register_all()) {
        printf("[hub] FAIL: home_failsafe_register_all\n");
        return false;
    }
#endif
#ifdef PROFILE_AGRICULTURE
    if (!agriculture_failsafe_register_all()) {
        printf("[hub] FAIL: agriculture_failsafe_register_all\n");
        return false;
    }
#endif
#ifdef PROFILE_HVAC
    if (!hvac_failsafe_register_all()) {
        printf("[hub] FAIL: hvac_failsafe_register_all\n");
        return false;
    }
#endif
#ifdef PROFILE_WATER_TREATMENT
    if (!water_failsafe_register_all()) {
        printf("[hub] FAIL: water_failsafe_register_all\n");
        return false;
    }
#endif

    /* Config portal. */
    if (!config_portal_init()) {
        printf("[hub] FAIL: config_portal_init\n");
        return false;
    }

    /* --- Ingestion pipeline (Phase 15.3 boot wiring) --- */

    /* Hub-side Gateway auth tracker (for frame HMAC verification).
     * Initialized with a default key — in production, populated after
     * Gateway attestation completes. The tracker starts unauthenticated;
     * gateway_tracker_init() sets up identity but not trust. */
    static const uint8_t zero_key[32] = {0};
    gateway_tracker_init(&s_hub_gw_tracker, 0xF0, zero_key);

    /* Hub-side Edge Node attestation trust tracker.
     * Starts empty — nodes are added as they complete attestation. */
    edge_tracker_init(&s_hub_edge_tracker, 0);  /* 0 = default expiry */

    /* Build the ingestion handler config.
     * Radio adapter and vtable selected at compile time per profile.
     * Skipping for bare-native builds (no profile defined). */
    /* --- Historian (Phase 16: offline-resilience storage) --- */
    historian_init();
    /* Key derived from attestation key in production. For now, use a
     * test key — production will call historian_set_key() with a key
     * derived via HMAC-SHA256(attestation_key, "historian_data_at_rest"). */

    /* --- Ingestion pipeline (Phase 15.3/16 boot wiring) --- */

    /* Hub-side Gateway auth tracker (for frame HMAC verification).
     * Initialized with a default key — in production, populated after
     * Gateway attestation completes. The tracker starts unauthenticated;
     * gateway_tracker_init() sets up identity but not trust. */
    static const uint8_t zero_key[32] = {0};
    gateway_tracker_init(&s_hub_gw_tracker, 0xF0, zero_key);

    /* Hub-side Edge Node attestation trust tracker.
     * Starts empty — nodes are added as they complete attestation. */
    edge_tracker_init(&s_hub_edge_tracker, 0);  /* 0 = default expiry */

    /* Build the ingestion handler config.
     * Radio adapter and vtable selected at compile time per profile.
     * Skipping for bare-native builds (no profile defined). */
#if defined(PROFILE_HOME) || defined(PROFILE_AGRICULTURE) || defined(PROFILE_HVAC) || defined(PROFILE_WATER_TREATMENT)
    {
        IngestionHandlerConfig_t ih_config;
        memset(&ih_config, 0, sizeof(ih_config));
        ih_config.gw_tracker     = &s_hub_gw_tracker;
        ih_config.edge_tracker   = &s_hub_edge_tracker;
        ih_config.history_lookup = NULL;  /* TODO: per-metric history buffers */

#ifdef PROFILE_HOME
        ih_config.vtable           = home_profile_get_vtable();
        ih_config.radio_receive    = zigbee_radio_adapter;
        ih_config.bounds_lookup    = home_get_validation_bounds;
        ih_config.domain_profile_id = 0;  /* Home */
        printf("[hub] Ingestion: Home profile — Zigbee radio, home vtable.\n");
#endif
#ifdef PROFILE_AGRICULTURE
        ih_config.vtable           = agriculture_profile_get_vtable();
        ih_config.radio_receive    = lora_radio_adapter;
        ih_config.bounds_lookup    = agriculture_get_validation_bounds;
        ih_config.domain_profile_id = 1;  /* Agriculture */
        printf("[hub] Ingestion: Agriculture profile — LoRa radio, agriculture vtable.\n");
#endif
#ifdef PROFILE_HVAC
        ih_config.vtable           = hvac_profile_get_vtable();
        ih_config.radio_receive    = lora_radio_adapter;
        ih_config.bounds_lookup    = hvac_get_validation_bounds;
        ih_config.domain_profile_id = 2;  /* HVAC */
        printf("[hub] Ingestion: HVAC profile — LoRa radio, hvac vtable.\n");
#endif
#ifdef PROFILE_WATER_TREATMENT
        ih_config.vtable           = water_profile_get_vtable();
        ih_config.radio_receive    = lora_radio_adapter;
        ih_config.bounds_lookup    = water_get_validation_bounds;
        ih_config.domain_profile_id = 3;  /* Water Treatment */
        printf("[hub] Ingestion: Water Treatment profile — LoRa radio, water vtable.\n");
#endif

        if (!ingestion_handler_init(&ih_config)) {
            printf("[hub] FAIL: ingestion_handler_init\n");
            return false;
        }
        printf("[hub] Ingestion pipeline initialised.\n");
    }
#else
    printf("[hub] No domain profile defined — ingestion handler skipped (native-only build).\n");
#endif

    printf("[hub] All subsystems initialised successfully.\n");
#ifdef PROFILE_HOME
    printf("[hub] Profile: HOME (PROFILE_HOME)\n");
    printf("[hub] Domain: Home/Building\n");
#endif
#ifdef PROFILE_AGRICULTURE
    printf("[hub] Profile: AGRICULTURE (PROFILE_AGRICULTURE)\n");
    printf("[hub] Domain: Agriculture\n");
#endif
#ifdef PROFILE_HVAC
    printf("[hub] Profile: HVAC (PROFILE_HVAC)\n");
    printf("[hub] Domain: HVAC\n");
#endif
#ifdef PROFILE_WATER_TREATMENT
    printf("[hub] Profile: WATER_TREATMENT (PROFILE_WATER_TREATMENT)\n");
    printf("[hub] Domain: Water Treatment\n");
#endif

    return true;
}

/* Native entry point — not compiled for ESP32 (app_main.cpp handles it). */
#ifndef PROFILE_HOME
int main(void)
{
    printf("=== Generic IoT Platform — Hub (native build) ===\n");
    if (!hub_init()) {
        printf("[hub] Initialisation FAILED.\n");
        return 1;
    }
    return 0;
}
#endif
