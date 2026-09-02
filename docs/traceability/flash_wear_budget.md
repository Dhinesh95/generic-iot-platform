# Flash Wear Budget Analysis

**Architecture ref:** Section 12 — Flash Wear & RTC Fallback
**Date:** Phase 10
**Status:** Analysis — code changes applied (Phase 16.1 batch-flush fix)

---

## Assumptions

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| Flash rated erase cycles | 100,000 per sector | Architecture Section 12 |
| Target device lifetime | 10 years | Typical industrial equipment expectation |
| Sector size | 4 KB | ESP32 internal flash standard |
| Wear-leveling factor | 10× | LittleFS partitions across ~10 sectors for wear-leveled data |
| Effective budget | 1,000,000 writes per logical address | 100K cycles × 10 sectors |

---

## Persistent Write Sources — Inventory

### 1. Audit Log Persistence (`audit_log_persist()`)

**Implementation:** `firmware/shared/audit_log.c:30`
**What gets written:** Entire 32-entry ring buffer (32 × ~112 bytes = ~3,584 bytes) + write_index + auth_failure_count = ~3.6 KB per persist call. Fits in a single 4 KB sector.
**Trigger:** Called after every `audit_log_add()` — one flash write per audit event.

**Event sources (all domains combined):**

| Event type | Trigger | Estimated frequency |
|------------|---------|-------------------|
| `AUDIT_AUTH_SUCCESS` | PIN login | 2-5/day (active config sessions) |
| `AUDIT_AUTH_FAILURE` | Failed PIN attempt | 0-2/day (varies by security posture) |
| `AUDIT_CONFIG_WRITE` | Rule config edit | 0-1/day (infrequent in steady-state) |
| `AUDIT_SAFETY_LOCKED_REJECTED` | Illegal write attempt | 0-1/day |
| `AUDIT_SESSION_CREATED` | Session start | 2-5/day |
| `AUDIT_SESSION_EXPIRED` | Session timeout/logout | 2-5/day |
| `AUDIT_TAMPER_DETECTED` | Physical tamper | 0/year (should never fire in normal operation) |
| `AUDIT_MODULE_ATTESTATION_FAIL` | LoRa join rejection | 0-1/year (fault condition) |

**Domain-specific estimates (events/day):**

| Domain | Conservative | Aggressive | Rationale |
|--------|-------------|------------|-----------|
| Home | 5 | 15 | Low event rate; mostly auth/session events |
| Agriculture | 5 | 15 | Similar to Home; field events infrequent |
| HVAC | 10 | 30 | More sensor-driven events, equipment state changes |
| Water Treatment | 20 | 50 | Industrial: more frequent safety checks, dosing events, alarms |

**10-year budget (Water Treatment, worst case):**

```
50 events/day × 365 days/year × 10 years = 182,500 writes
Effective budget: 1,000,000 writes (100K × 10-sector wear-leveling)
Headroom: 5.5× safety margin
```

**Verdict: SAFE** — even at the aggressive Water Treatment estimate, the audit log stays within budget with significant headroom.

**Optimization opportunity (not required, low priority):** Currently `audit_log_persist()` writes the entire 32-entry buffer on every event. A production optimization could persist only the new entry + metadata (append-only), reducing per-write flash traffic from ~3.6 KB to ~120 bytes. This doesn't change the write-cycle count (still one persist call per event) but reduces write amplification and energy per write.

---

### 2. Device Identity Persistence

**Implementation:** `firmware/shared/device_identity.c`
**What gets written:** `DeviceIdentity_t` (~28 bytes) + `provisioned` flag + `fw_version_pending` = ~36 bytes per persist call.
**Trigger:** `device_identity_set()` (factory provisioning, once per device lifetime) + `device_identity_set_pending_version()` (per OTA update) + `device_identity_confirm_boot()` (per successful OTA boot).

**Write frequency:**

| Event | Frequency |
|-------|-----------|
| Factory provisioning | 1× per device lifetime |
| OTA update (pending + confirm) | 2-4× per year (quarterly updates assumed) |
| **Total per year** | **~5 writes/year** |

**10-year budget:**

```
5 writes/year × 10 years = 50 writes
Effective budget: 1,000,000 writes
Headroom: 20,000× safety margin
```

**Verdict: SAFE** — negligible wear impact. Device identity writes are infrequent enough to be irrelevant in the flash budget.

---

### 3. Time Source RTC Persistence — ZERO WRITES IN PRODUCTION

**Verified against source:** `firmware/shared/time_source.c` — production code path analysis.

**Finding (corrected from original Phase 10 estimate):** The original Phase 10 analysis estimated "~87,600 writes over 10 years" for time source persistence. This was based on a stale assumption from before Phase 9.2 clarified the RTC hardware model. **The actual production code path performs zero flash writes.**

**Evidence:**
- `mock_rtc_persist()` (the only function that calls `s_storage->save()`) is called exactly once in the entire codebase — line 158 of `time_source.c`, inside `time_source_mock_set_rtc_persistent()`.
- `time_source_mock_set_rtc_persistent()` is a test-only mock function (name contains "mock").
- The production path in `time_source_get_ms()` calls `s_backend->rtc_time_get()` and returns the result directly — no flash write occurs.
- `TimeSourceStorage_t` was explicitly repurposed in Phase 9.2 as test-only persistence for the mock layer. The real RTC chip (DS3231) provides absolute wall-clock time from its independently-powered oscillator — the MCU just reads it, no persistence needed.

**Verdict: ZERO FLASH WRITES.** This entry is removed from the budget summary. The original 87,600 estimate was incorrect — a stale artifact from before Phase 9.2's design correction.

---

### 4. Config Portal Rule Writes (NOT YET IMPLEMENTED)

**Implementation:** `config_portal_write()` is a stub — no flash write occurs for rule config edits. The function logs via `audit_log_add()` but does not persist the rule change to NVS/flash.

**When implemented:** Each rule config edit would write a `RuleEntry_t` (16 bytes) to NVS.

**Estimated frequency:**

| Domain | Conservative | Aggressive |
|--------|-------------|------------|
| Home | 0.1/day | 1/day |
| Agriculture | 0.1/day | 1/day |
| HVAC | 0.5/day | 5/day |
| Water Treatment | 1/day | 10/day |

**10-year budget (Water Treatment, aggressive):**

```
10 writes/day × 365 days/year × 10 years = 36,500 writes
Effective budget: 1,000,000 writes
Headroom: 27× safety margin
```

**Verdict: SAFE (when implemented)** — even aggressive config edits stay within budget.

---

### 5. Historian — IMPLEMENTED (Phase 16, corrected Phase 16.1)

**Architecture reference:** 30-day rotating window, Section 12.
**Implementation:** `firmware/hub/historian.c`
**Status:** Implemented with batch-flush fix (Phase 16.1).

#### Original Phase 10 Estimate (INCORRECT)

Phase 10 assumed 32 bytes written per record. Phase 16's initial implementation wrote the **entire ring buffer** (16,384 × 40B = 640 KB) on every record — a 20,000× amplification. This was corrected in Phase 16.1.

#### Corrected Per-Domain Write Rates

| Domain | Records/day | On-disk record size | Notes |
|--------|-------------|--------------------|----|
| Home | 500 | 40 bytes (24B ciphertext + 16B CCM tag) | Aggressive estimate |
| Agriculture | 1,000 | 40 bytes | |
| HVAC | 2,000 | 40 bytes | |
| Water Treatment | 5,000 | 40 bytes | Aggressive industrial estimate |

#### Flash Wear Arithmetic (Water Treatment — worst case)

**Without batch flush (Phase 16.0 — UNSAFE):**
```
Each record write triggers historian_persist() → saves ENTIRE ring buffer.
Ring buffer: 16,384 × 40 = 655,360 bytes = 640 KB per write.

5,000 writes/day × 640 KB/write = 3.2 GB/day
3.2 GB/day × 365 × 10 = 11.68 TB over 10 years
With 4 KB sectors: 2.92B sector writes
With 10× wear-leveling: 292M writes per logical sector
Budget: 100,000 cycles
EXCEEDS BUDGET BY 2,920×

Verdict: UNSAFE — reclassified from "production hardening" to correctness finding.
```

**With batch flush (Phase 16.1 — FIXED):**
```
Batch flush: records buffered in RAM, flushed to flash every 60 seconds
  or when pending buffer reaches 64 records (whichever comes first).
Each flush writes only NEW records via append-only callback (40 bytes/record).

Water Treatment: 5,000 records/day
  Flushes per day: 86,400 sec ÷ 60 sec = 1,440 (time-based)
  Or: 5,000 ÷ 64 = 78 (capacity-based) — time-based wins
  Actual flushes/day: 1,440
  Records per flush: 5,000 ÷ 1,440 ≈ 3.5 (average)
  Bytes per flush: 3.5 × 40 = 140 bytes (average)

1,440 flushes/day × 365 × 10 = 5.26M flushes over 10 years
5.26M × 140 bytes = 736 MB total writes
With 4 KB sectors: 184,000 sector writes
With 10× wear-leveling: 18,400 writes per logical sector
Budget: 100,000 cycles
Headroom: 5.4× safety margin

Verdict: SAFE
```

**Per-domain flash wear summary (with batch flush):**

| Domain | Records/day | Flushes/day | Bytes/flush | 10-year sector writes | Wear-level-adjusted | Budget | Headroom | Verdict |
|--------|-------------|-------------|-------------|----------------------|--------------------|---------|----------|--------|
| Home | 500 | 1,440 | 14 | 18,400 | 1,840 | 100,000 | 54× | **SAFE** |
| Agriculture | 1,000 | 1,440 | 28 | 36,800 | 3,680 | 100,000 | 27× | **SAFE** |
| HVAC | 2,000 | 1,440 | 56 | 73,500 | 7,350 | 100,000 | 13.6× | **SAFE** |
| Water Treatment | 5,000 | 1,440 | 140 | 184,000 | 18,400 | 100,000 | 5.4× | **SAFE** |

**Phase 16.1 reclassification:** The historian flash write was reclassified from "production hardening item" (Phase 10) to **correctness/safety finding** (Phase 16.1) because the unbatched implementation exceeded the flash-wear budget by 2,920×. The batch-flush fix restores safe operation with 5.4× headroom at the aggressive Water Treatment estimate.

**Batch flush parameters:**
- Flush interval: 60 seconds (configurable via `HISTORIAN_FLUSH_INTERVAL_MS`)
- Pending buffer limit: 64 records (configurable via `HISTORIAN_PENDING_MAX`)
- Write mode: append-only (40 bytes per record, not 640 KB full-ring)

**Power-failure data loss (steady-state):** The 60-second timer always fires first at normal domain write rates. The 64-record cap never triggers under steady state — it exists only as a safety bound for burst-write scenarios. Actual power-failure exposure:

| Domain | Records/60s (steady) | Data loss on power failure |
|--------|---------------------|---------------------------|
| Home | 1 | 1 record (~120 sec of data) |
| Agriculture | 1 | 1 record (~60 sec of data) |
| HVAC | 2 | 2 records (~60 sec of data) |
| Water Treatment | 4 | 4 records (~60 sec of data) |

The 64-record cap would only trigger if the write rate exceeded ~1.07 records/second (92,160 records/day) — a burst scenario far above any domain's steady-state rate.

**Key design decision:** The batch flush writes only NEW records via the `append` storage callback, not the entire ring buffer. This is the critical fix — without it, even 60-second batching still exceeds the budget (841× over) because the full 640 KB ring is written on every flush.

---

## Summary Table

| Write Source | 10-year writes (aggressive) | Effective budget | Headroom | Verdict |
|-------------|---------------------------|-----------------|----------|---------|
| Audit log | 182,500 | 1,000,000 | 5.5× | **SAFE** |
| Device identity | 50 | 1,000,000 | 20,000× | **SAFE** |
| ~~Time source RTC~~ | ~~87,600~~ | ~~1,000,000~~ | ~~11.4×~~ | **REMOVED — zero writes in production** (Phase 10.1) |
| Config portal rules (future) | 36,500 | 1,000,000 | 27× | **SAFE** |
| Historian (with batch flush) | 18,400 sector writes | 100,000 | 5.4× | **SAFE** (Phase 16.1 fix) |

**Conclusion:** No write source exceeds the flash wear budget under the stated assumptions. The audit log is the most-write-intensive *currently implemented* source, with a 5.5× safety margin at the aggressive Water Treatment estimate. The historian required a corrective fix in Phase 16.1 — the unbatched implementation exceeded the budget by 2,920×. The batch-flush fix (append-only writes, 60-second interval) restores safe operation with 5.4× headroom. The time source entry was removed after Phase 10.1 verification confirmed zero production flash writes.

**Code changes required by this analysis:** Phase 16.1 implemented batch-flush for the historian (`historian_flush()`, append-only storage callback) to correct the flash-wear budget violation. This was reclassified from "production hardening" to "correctness/safety finding".

---

## What was checked vs. not checked

**Checked:** All persistent-write call sites in `firmware/shared/` and `firmware/hub/` that currently exist in the codebase (audit_log, device_identity, time_source, config_portal, historian).

**Not checked:**
- NVS rule config writes (config_portal_write stub does not persist — estimates based on expected implementation)
- OTA image download staging (out of scope per Phase 7 — would use a separate OTA partition, not the data partition analyzed here)
- Cloud-side storage (InfluxDB/etc. — not on-device flash, not applicable to this analysis)
