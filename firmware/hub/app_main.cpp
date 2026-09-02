/**
 * @file app_main.cpp
 * @brief Arduino entry points (setup/loop) for ESP32 target.
 *
 * This file provides the Arduino framework's required setup() and
 * loop() functions, which call into the C hub_init() function.
 *
 * This file is ONLY compiled for the ESP32 target.
 * The native/host test build uses main.c instead.
 */

#if defined(PROFILE_HOME) || defined(PROFILE_AGRICULTURE) || defined(PROFILE_HVAC) || defined(PROFILE_WATER_TREATMENT)

#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "config_portal.h"
#include "ingestion_handler.h"

/* Defined in main.c — initialise all subsystems. */
extern bool hub_init(void);

#ifdef __cplusplus
}
#endif

void setup(void)
{
    Serial.begin(115200);
#ifdef PROFILE_HOME
    Serial.println("=== Generic IoT Platform — Hub (Home/Building) ===");
#endif
#ifdef PROFILE_AGRICULTURE
    Serial.println("=== Generic IoT Platform — Hub (Agriculture) ===");
#endif
#ifdef PROFILE_HVAC
    Serial.println("=== Generic IoT Platform — Hub (HVAC) ===");
#endif
#ifdef PROFILE_WATER_TREATMENT
    Serial.println("=== Generic IoT Platform — Hub (Water Treatment) ===");
#endif

    if (!hub_init()) {
        Serial.println("[hub] Initialisation FAILED.");
        return;
    }

    Serial.println("[hub] Hub ready.");
}

void loop(void)
{
    /*
     * Main loop — Phase 15.3 boot wiring.
     * The ingestion handler polls the radio for Gateway batch frames,
     * verifies HMAC + Edge Node attestation, validates sensor readings,
     * and evaluates domain rules — the full 3-tier pipeline.
     *
     * Rate-limited to once per second to avoid starving other tasks
     * (config portal, MQTT telemetry, safety monitor).
     */
    static uint32_t last_poll_ms = 0;
    uint32_t now_ms = millis();

    if ((now_ms - last_poll_ms) >= 1000) {
        last_poll_ms = now_ms;
        ingestion_handler_poll((uint64_t)now_ms);
    }

    /* Other periodic tasks (MQTT, config portal, safety monitor) go here. */
    delay(100);
}

#endif /* PROFILE_HOME || PROFILE_AGRICULTURE || PROFILE_HVAC || PROFILE_WATER_TREATMENT */
