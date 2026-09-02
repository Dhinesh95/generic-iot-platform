/**
 * @file time_source.c
 * @brief Time source implementation — NTP/RTC fallback.
 *
 * Architecture ref: Section 12 (RTC fallback, NTP sync).
 *
 * Real-hardware model (DS3231 or similar):
 *   - RTC chip provides absolute wall-clock time directly from its own
 *     independently-powered oscillator. The MCU just reads it — no
 *     delta arithmetic, no MCU-side persistence needed.
 *   - On boot: read RTC chip → get correct wall-clock time immediately.
 *   - NTP sync: when available, NTP takes priority over RTC.
 *
 * Mock model (for testing):
 *   - time_source_mock_set_rtc_persistent(ms) simulates "what the RTC
 *     chip reports right now" — tests advance this between simulated
 *     reboots to simulate time passage during outages.
 *   - TimeSourceStorage_t persists the mock RTC value across simulated
 *     reboots so tests can verify cross-reboot timestamp coherence
 *     without real hardware.
 *
 * Key design decision: the MCU NEVER computes elapsed time from a
 * persisted reference. The RTC chip IS the source of truth after power
 * loss. The storage mechanism exists only for the mock layer.
 */

#include "time_source.h"
#include <string.h>

/* ---------- Internal state ---------- */

static TimeSourceState_t s_state = TIME_SRC_UNINITIALISED;
static const TimeSourceBackend_t *s_backend = NULL;
static const TimeSourceStorage_t *s_storage = NULL;

/* Mock state. */
static uint64_t s_mock_time = 0;
static bool s_mock_reliable = false;
static bool s_mock_active = false;

/* Mock RTC persistent state — simulates what the RTC chip reports. */
static uint64_t s_mock_rtc_persistent = 0;
static bool s_mock_rtc_persistent_loaded = false;

/* ---------- Internal helpers ---------- */

static void mock_rtc_persist(void)
{
    if (s_storage && s_storage->save) {
        s_storage->save(s_mock_rtc_persistent);
    }
}

/* ---------- Public API ---------- */

void time_source_set_storage(const TimeSourceStorage_t *storage)
{
    s_storage = storage;
}

bool time_source_init(void)
{
    s_state = TIME_SRC_UNINITIALISED;
    s_mock_active = false;
    s_mock_time = 0;
    s_mock_reliable = false;
    s_mock_rtc_persistent_loaded = false;

    /* Load persisted mock RTC value if storage backend is registered.
     * This simulates what the RTC chip would report on first read
     * after a power cycle — the chip has been ticking independently. */
    if (s_storage && s_storage->load) {
        uint64_t loaded_rtc = 0;
        if (s_storage->load(&loaded_rtc)) {
            s_mock_rtc_persistent = loaded_rtc;
            s_mock_rtc_persistent_loaded = (loaded_rtc > 0);
        }
    } else {
        s_mock_rtc_persistent = 0;
    }

    return true;
}

void time_source_set_backend(const TimeSourceBackend_t *backend)
{
    s_backend = backend;
}

uint64_t time_source_get_ms(void)
{
    /* Mock takes priority if set. */
    if (s_mock_active) {
        s_state = s_mock_reliable ? TIME_SRC_NTP_ACTIVE : TIME_SRC_UNRELIABLE;
        return s_mock_time;
    }

    /* Fallback priority: NTP → RTC → unreliable. */
    if (s_backend) {
        /* Check NTP first. */
        if (s_backend->ntp_is_synced && s_backend->ntp_is_synced()) {
            uint64_t ntp_time = s_backend->ntp_time_get ? s_backend->ntp_time_get() : 0;
            if (ntp_time > 0) {
                s_state = TIME_SRC_NTP_ACTIVE;
                return ntp_time;
            }
        }

        /* NTP unavailable/stale — check RTC. */
        if (s_backend->rtc_is_available && s_backend->rtc_is_available()) {
            uint64_t rtc_time = s_backend->rtc_time_get ? s_backend->rtc_time_get() : 0;
            if (rtc_time > 0) {
                /*
                 * Real-hardware path: the RTC chip provides absolute
                 * wall-clock time directly. The MCU just reads it.
                 * No delta arithmetic — the chip's oscillator has been
                 * ticking independently during the power outage.
                 */
                s_state = TIME_SRC_RTC_ONLY;
                return rtc_time;
            }
        }
    }

    /* No reliable source available. */
    s_state = TIME_SRC_UNRELIABLE;
    return 0;
}

bool time_source_is_reliable(void)
{
    return (s_state == TIME_SRC_NTP_ACTIVE || s_state == TIME_SRC_RTC_ONLY);
}

TimeSourceState_t time_source_get_state(void)
{
    return s_state;
}

void time_source_mock_set(uint64_t ms, bool reliable)
{
    s_mock_time = ms;
    s_mock_reliable = reliable;
    s_mock_active = true;
}

void time_source_mock_advance(uint64_t delta_ms)
{
    if (s_mock_active) {
        s_mock_time += delta_ms;
    }
}

void time_source_mock_set_rtc_persistent(uint64_t ms)
{
    s_mock_rtc_persistent = ms;
    s_mock_rtc_persistent_loaded = true;
    mock_rtc_persist();
}
