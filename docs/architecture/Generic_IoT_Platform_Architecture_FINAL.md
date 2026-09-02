# Generic Multi-Domain IoT Platform — Architecture (v1)

> **Status**: Draft — Layers 1-6 approved. Layers 7+ (Edge hardware/firmware
> detail, sensor validation, requirements traceability) pending.
> **Scope confirmed with stakeholder**: Multi-domain (Industrial, Agriculture,
> Home/Building) from day 1. Scale: small (10 nodes) to large (5000+ nodes).
> Connectivity: mixed wired + wireless. Power: domain-dependent
> (mains/battery/solar). Certification: domain-dependent (some domains
> safety-critical, some not).

---

## How to Read This Document

Every architectural decision below states **what** was chosen and **why**,
including alternatives that were considered and rejected. Trade-offs are
called out explicitly rather than hidden. This document does **not** claim
"all gaps resolved" — open items and follow-up layers are listed at the end.

---

## 1. Assumptions & Constraints (Locked)

| Parameter | Value | Architectural Consequence |
|---|---|---|
| Domain | Multi: Industrial, Agriculture, Home/Building (extensible) | Core + domain-profile split is mandatory, not optional |
| Scale | 10 → 5000+ nodes | Hierarchical/federated design required; flat topology will not scale |
| Connectivity | Mixed wired + wireless | Multi-tier bridge architecture; connectivity choice made per domain/site |
| Power | Domain-dependent (mains / battery / solar) | Power-management HAL abstraction, not hardcoded duty cycle |
| Certification | Domain-dependent | Safety-criticality tiering required; certification scope must be isolatable per domain |

**Delivery note (not an architecture constraint, a rollout recommendation):**
the architecture is designed to support all domains from day 1. It does
**not** follow that all domain profiles must be field-ready or certified
on the same launch date. Profiles are independently deployable — Profile A
can ship while Profile B is still in test, because of the isolation
described in Section 4.

---

## 2. Tier Architecture

```
┌─────────────────────────────────────────────────────────┐
│  TIER 4 — Cloud (domain-agnostic, multi-tenant)          │
│  Analytics, OTA orchestration, fleet management          │
└───────────────────────┬─────────────────────────────────┘
                         │ MQTT/TLS
┌───────────────────────┴─────────────────────────────────┐
│  TIER 3 — Hub (domain-agnostic core + domain profile FW) │
│  1 hardware family, pluggable power variant               │
└───────────────────────┬─────────────────────────────────┘
                         │ Wireless (LoRa/Zigbee) or Wired (Ethernet)
                         │ — chosen per site at commissioning
┌───────────────────────┴─────────────────────────────────┐
│  TIER 2 — Gateway (fog computing, domain-agnostic)        │
│  Universal baseboard + pluggable radio module             │
└───────────────────────┬─────────────────────────────────┘
                         │ RS-485 (wired) or wireless mesh
┌───────────────────────┴─────────────────────────────────┐
│  TIER 1 — Edge Node (domain-agnostic HW, profile FW)      │
│  Power variant: mains / battery / solar                   │
└───────────────────────────────────────────────────────────┘
```

**Why this structure:**
- Safety **decision-making** lives only at Tier 3 (Hub). Tier 1/2 do sensing,
  actuation I/O, and fog computing but hold no safety logic. This confines
  certification scope to Tier 3 — Edge/Gateway never need domain
  certification.
- Tier 3↔4 (Hub↔Cloud) is the one link that can be fully domain-agnostic
  (MQTT/TLS payload shape doesn't change by domain), so multi-tenancy and
  fleet orchestration live entirely at this boundary.

---

## 3. Communication Architecture

### Tier 1↔2 (Edge↔Gateway)

| Domain | Protocol | Rationale |
|---|---|---|
| Industrial | RS-485 | Noise-immune, deterministic, proven in EMI-heavy environments |
| Agriculture | Wireless mesh (802.15.4/BLE mesh) | Wiring impractical in open field, sparse nodes |
| Home/Building | Zigbee | Dense retrofit environment, established ecosystem |

### Tier 2↔3 (Gateway↔Hub)

| Domain | Protocol | Rationale |
|---|---|---|
| Industrial | LoRa (or wired Ethernet if facility pre-wired) | Long range, large facility footprint |
| Agriculture | LoRa | Long range, low power, license-free, cost-effective vs cellular |
| Home/Building | Zigbee | Dense short-range, no long-range requirement |

**Hardware decision — Pluggable Radio Module (approved):**
- Gateway = universal baseboard (MCU, RS-485 PHY, power input, standard
  radio module connector) + swappable radio module per domain.
- Industrial and Agriculture profiles **reuse the same LoRa module**
  (different firmware profile, same radio hardware) — reduces module SKU
  count to two: LoRa module, Zigbee module.
- Radio modules are FCC/CE certified at the **module level**, independent of
  baseboard revisions — baseboard changes do not trigger re-certification.

**Trade-off accepted:** module + connector overhead costs more per unit
than a single soldered radio, in exchange for certification reuse and the
ability to re-purpose a device for a different domain by swapping the
module.

### Tier 3↔4 (Hub↔Cloud)

MQTT/TLS, domain-agnostic. This is the only fully generic link in the
communication stack.

---

## 4. Security Architecture (Threat-Model-First)

### Threat Model

| # | Threat | Most Relevant To | Impact if Unaddressed |
|---|---|---|---|
| T1 | Rogue node spoofing | All domains | False data drives wrong actuator decision |
| T2 | Wireless MITM | All, wireless-heavy domains more | Command injection, data tampering |
| T3 | Physical tamper / JTAG dump | Industrial, Agriculture (unattended) | Firmware/key extraction |
| T4 | Compromised node → lateral movement | All | Full-system compromise from one weak node |
| T5 | Malicious/corrupted OTA | All | Fleet-wide compromise from one bad update |
| T6 | Field misconfiguration | Safety-critical domains | Safety logic bypass → physical harm |
| T7 | Cloud multi-tenant data breach | All, especially Home (privacy) | Cross-tenant data exposure |
| T8 | DoS / radio jamming | Industrial (adversarial environments) | Loss of monitoring/control |
| T9 | Replay attack | All | Unauthorized re-trigger of an action |
| T10 | Supply-chain risk in pluggable radio module | All (introduced by our own Tier-2 design) | Backdoor at module level |

### Threat → Mechanism Mapping

| Threat | Mechanism |
|---|---|
| T1 | Zero-trust attestation (HMAC-SHA256 challenge-response) |
| T2 | Transport encryption (AES-128-CCM), per-session keys |
| T3 | Secure Boot + Flash Encryption + JTAG fuse-burn disable + tamper switch (industrial/agri) |
| T4 | RBAC / per-node capability bitmap + network segmentation |
| T5 | HMAC-SHA256 firmware verification + canary rollout (per-profile, Section 5) |
| T6 | Safety-locked vs operational rule split (Section 6) + authenticated config portal |
| T7 | Per-tenant data isolation (Section 6) |
| T8 | Rate limiting + LoRa channel diversity + graceful degrade to last-known-state |
| T9 | Nonce + timestamp per frame |
| T10 | Module-level attestation — radio module authenticates to baseboard before being trusted |

### Domain-Based Security Tiering

| Tier | Mechanisms | Applies To |
|---|---|---|
| **Baseline (mandatory, all domains)** | Attestation, transport encryption, secure boot, firmware HMAC verify, replay protection, config-portal RBAC | Every domain, no exceptions |
| **Enhanced (domain-selectable)** | Tamper switch, JTAG fuse-burn, module attestation, audit-log HMAC chain, hard cloud tenant isolation | Mandatory for Industrial/Agriculture; optional (cost trade-off) for Home |

---

## 5. OTA / Fleet Management

**Problem specific to multi-domain fleets:** a single canary/rollout
strategy across the whole fleet risks pushing the wrong firmware to the
wrong domain, or letting one domain's bad update affect fleet-wide
rollout confidence.

**Design:**
- Every device's identity record carries **immutable, factory-set**
  `domain_profile_id` and `hw_variant_id` fields (alongside the existing
  UUID).
- Cloud OTA pipeline **must** verify `domain_profile_id`/`hw_variant_id`
  match between device and firmware manifest before accepting a push;
  mismatches are rejected outright, never silently skipped.
- Canary rollout (5% → monitor → 95%, existing pattern) runs
  **independently per (domain_profile_id, hw_variant_id) group**. A
  canary failure in one domain rolls back only that domain's fleet.
- Large fleets (500-5000+ nodes) require **site/region-aware staged
  rollout**, not just percentage-based — pushing OTA to every node behind
  one Gateway simultaneously can saturate LoRa bandwidth and delay safety
  telemetry. OTA traffic priority is always below safety-telemetry
  priority.
- Rollback events are audit-logged with `domain_profile_id` attached, so
  fleet-wide forensics can attribute an incident to a specific domain.

---

## 6. Safety / Rule Logic Architecture

### Core vs Domain-Profile Split

| Component | Core (generic) | Domain Profile |
|---|---|---|
| Rule evaluation engine (timing, priority tiers) | ✅ | |
| Rule data (thresholds, actuator mapping) | | ✅ |
| Interlock state-machine mechanics | ✅ | |
| Interlock sequence definitions | | ✅ |
| Sensor validation mechanism | ✅ | |
| Sensor plausibility bounds | | ✅ |
| Actuator fail-safe mechanism (configurable mode) | ✅ | |
| Actuator fail-safe mode per actuator | | ✅ |
| RBAC / rule-lock enforcement | ✅ | |
| Rule criticality tagging mechanism | ✅ | |
| Which rules are safety-locked | | ✅ |

### Interface Pattern (vtable-based)

The generic rule engine calls into a domain profile through a fixed
vtable (`getRuleTable`, `validateSensorReading`, `getFailSafeMode`,
`executeAction`). The engine itself contains no domain knowledge. A
domain profile is a self-contained source file selected at build time via
a PlatformIO environment (`env:hub_water`, `env:hub_hvac`,
`env:hub_home`, ...), not a runtime switch.

**Why this matters for the confirmed scope:**
- **Certification isolation** — certifying one domain profile only
  requires reviewing the shared core plus that one profile file; other
  profiles are untouched and unaffected.
- **Bug isolation** — a fix to one domain's rules does not require
  rebuilding, retesting, or redeploying other domains' binaries.
- **Extensibility for day-1 multi-domain scope** — adding a future domain
  (e.g. logistics) means adding a new profile folder; the core engine is
  not touched.
- **Field trust model** — `SAFETY_LOCKED` rules are baked into the domain
  profile at factory build time; the field config portal (Section 4, T6)
  can never mutate them, only view them.

**Trade-off accepted:** vtable dispatch has negligible runtime overhead;
the real cost is discipline — code review must catch any domain-specific
value or logic accidentally added to core files.

---

## 7. Data Model & Cloud Multi-Tenancy

**Problem:** a single-tenant schema does not fit "small to large, all
customers" scope.

**Schema (InfluxDB, revised for multi-tenancy):**

```
Measurement: node_telemetry
Tags: tenant_id, site_id, node_id, device_class, metric_id, domain_profile_id
Fields: value (float)
```

**Isolation tiers (mirrors the security tiering in Section 4):**

| Isolation Level | Mechanism | Cost | Fit |
|---|---|---|---|
| Shared DB, tag-filtered | Single instance, `tenant_id` tag | Low | Small/medium customers, non-regulated domains |
| Bucket-per-tenant | InfluxDB 2.x native bucket isolation | Medium | Medium/large customers |
| Instance-per-tenant | Dedicated InfluxDB/Grafana | High | Large enterprise with compliance requirements (e.g. regulated water utility) |

**Cardinality note:** the single-deployment estimate (~200K series) scales
with tenant count. At roughly 100 tenants this approaches ~20M series,
near the practical ceiling for a single InfluxDB OSS instance — this is
the point at which bucket-per-tenant or sharding becomes mandatory rather
than optional, and should be planned for rather than discovered in
production.

**MQTT topic namespace:** `rainmaker/{tenant_id}/{site_id}/{device_id}/telemetry`
— tenant_id is extracted from the topic path into the InfluxDB tag at
the Telegraf layer.

**Access control:** Grafana organizations (1 per tenant), each scoped to
its own InfluxDB bucket via a bucket-scoped token — enforced at the
database token level, not only the UI level, since UI-only isolation is
bypassable via direct API access.

---

## 8. Edge Node Hardware / Firmware Detail

**Confirmed constraints:** single MCU family across all domains (existing
PY32F030x8 class: 64KB flash, 8KB RAM); fully pluggable/dynamic sensor
driver support.

**Tension to resolve:** a single low-RAM MCU family cannot safely support
a literal heap-based "dynamic" driver-loading architecture — fragmentation
risk is unacceptable on an 8KB-RAM safety-adjacent device. The
requirement is reinterpreted as **field-configurable, not
runtime-code-loading**, which the existing `universal_sensor.h` /
`universal_config.h` pattern already implements correctly.

### Two Layers of "Pluggable"

| Layer | Decided At | Mechanism |
|---|---|---|
| Which drivers exist in this firmware | Build time, per domain profile | Conditional compilation (`#ifdef PROFILE_X`) — only domain-relevant drivers are compiled in |
| Which driver is active on a given node | Field config time | LittleFS `UniversalConfigRecord` — no reflash required to change a node's sensor type |

```c
// shared/edge/sensor_driver.h — static, no heap allocation
typedef struct {
    SensorType_t   type;
    bool           (*init)(SensorConfig_t* cfg);
    SensorReading_t(*read)(void);
    bool           (*validate)(float raw_value);
} SensorDriver_t;

static const SensorDriver_t driver_registry[] = {
    { SENSOR_DISTANCE, distanceSensorInit, distanceSensorRead, distanceSensorValidate },
    { SENSOR_ANALOG,   analogSensorInit,   analogSensorRead,   analogSensorValidate   },
    { SENSOR_I2C,      i2cSensorInit,      i2cSensorRead,      i2cSensorValidate      },
    // only drivers relevant to this domain profile are compiled in
};
```

**Design rule:** only one sensor driver's working memory is resident in
RAM at a time per node (existing pattern) — this delivers full field
flexibility at effectively zero extra RAM cost.

**RAM budget check (8KB PY32, indicative):**

```
RTOS/stack overhead:            ~1.5KB
RS-485/wireless buffer:         ~1KB
Sensor driver working buffer:   ~0.5KB (single active driver)
Config struct (in-RAM copy):    ~0.1KB
Attestation/crypto working:     ~0.5KB
Application state:              ~0.5KB
-----------------------------------------
Headroom remaining:             ~4KB
```

**Power variants (mains/battery/solar):** handled as a firmware-level
`PowerProfile_t` HAL configuration (active/sleep duration, low-battery
threshold), not a hardware or MCU change — same pattern style as the
domain-profile vtable in Section 6.

**Trade-off accepted:** this is a medium-complexity hybrid (static
registry + config selection) rather than the simplest fixed-driver-list
option or a true dynamic loader — chosen because it satisfies field
flexibility without introducing heap-fragmentation risk on a
safety-adjacent, memory-constrained MCU.

---

## 9. Sensor Validation / Plausibility Design

**Purpose:** the rule engine (Section 6) can only be as trustworthy as
its inputs. A disconnected, stuck, drifting, or spiking sensor must be
caught before it reaches rule evaluation — otherwise correct safety logic
still produces an unsafe outcome from bad data.

### Mechanism (Core, Generic)

```c
// shared/sensor_validation.h
typedef struct {
    float    min_physical;
    float    max_physical;
    float    max_rate_of_change;    // per second
    uint16_t stuck_timeout_sec;
    uint8_t  cross_check_node_id;   // 0 = no redundant sensor to correlate with
} SensorValidationBounds_t;

typedef enum {
    SENSOR_VALID,
    SENSOR_OUT_OF_PHYSICAL_RANGE,
    SENSOR_RATE_EXCEEDED,
    SENSOR_STUCK,
    SENSOR_CROSS_CHECK_MISMATCH
} SensorValidationResult_t;

SensorValidationResult_t sensorValidate(
    uint8_t node_id, uint8_t metric_id, float new_value,
    SensorValidationBounds_t* bounds,  // supplied by domain profile
    SensorHistory_t* history           // rolling buffer, core-managed
);
```

### Bounds (Domain Profile, Data Not Code)

Bounds — physical range, plausible rate of change, and how long a
"stuck" value is tolerated before being treated as suspect — are
domain-specific data. A chlorine reading unchanged for an hour is
suspect (dosing/consumption should cause drift); a stable HVAC room
temperature unchanged for an hour is normal. Hardcoding one domain's
tolerance into the core engine would cause false positives in one domain
and missed faults in another.

### Failure Handling — Critical Safety Principle

When validation fails, the engine must treat the reading as
**unknown/unsafe and route to the domain's fail-safe mode** (Section 6) —
it must never continue operating on the last-known-good value as if it
were still valid. A stuck "chlorine level fine" reading must not be
allowed to let a dosing pump continue indefinitely. Every validation
failure is recorded via the audit log.

### Placement

Validation runs as a gate between raw sensor polling and rule
evaluation, not after:

```
sensorPollTask (1000ms) → raw reading
    → sensorValidate() [gate]
        → VALID   → safetyMonitorTask / motorControlTask (existing, unchanged)
        → INVALID → audit log + domain fail-safe trigger (bypasses normal rule path)
```

This does not change existing task timing budgets (50ms/100ms) —
validation is a lightweight bounds/history comparison.

**Trade-offs:** bounds set too tight will false-positive on legitimate
but unusual readings — bounds must come from domain-expert review and
real field data, not estimation. Rolling-history memory for validation
should reuse the existing per-node metric cache (`stateful_cache.h`)
rather than duplicate storage, to stay within Gateway RAM budget at
scale (960 nodes × 16 metrics).

---

## 10. Actuator Fail-Safe Decision Tables (Per Domain)

### Fail-Safe Modes (Generic, Core)

```c
// shared/actuator_failsafe.h
typedef enum {
    FAILSAFE_DE_ENERGIZE,    // actuator goes to its de-energized physical state
    FAILSAFE_HOLD_LAST,      // retains last commanded state (requires local energy storage)
    FAILSAFE_FORCE_OFF,      // forced OFF regardless of last state
    FAILSAFE_FORCE_SAFE_POS  // driven to a specific safe position, not just de-energized
} ActuatorFailSafeMode_t;

typedef struct {
    uint8_t                actuator_id;
    ActuatorSafetyClass_t  criticality;       // aligns with Section 6 rule tiers
    ActuatorFailSafeMode_t power_loss_mode;   // Hub/Gateway/actuator power lost
    ActuatorFailSafeMode_t comms_loss_mode;   // comms to Hub lost, actuator still powered
    uint16_t               comms_timeout_sec; // delay before comms_loss_mode triggers
} ActuatorFailSafeEntry_t;
```

**Power loss and comms loss are distinct scenarios requiring distinct
responses** — an actuator with local power but a lost LoRa link to the
Hub must not simply freeze on a stale command; a full power loss is a
different failure mode entirely and is handled separately.

### Per-Domain Tables (Illustrative)

**Water Treatment**

| Actuator | Criticality | Power-Loss Mode | Comms-Loss Mode | Reasoning |
|---|---|---|---|---|
| Dosing valve (chlorine) | CRITICAL | FORCE_OFF | FORCE_OFF (10s) | Unmonitored dosing risks over/under-dose; default to no dosing when uncertain |
| Main supply valve | CRITICAL | DE_ENERGIZE (normally-closed) | HOLD_LAST (60s) → DE_ENERGIZE | Brief comms blips shouldn't cut supply; sustained loss should |
| Circulation pump | STANDARD | FORCE_OFF | HOLD_LAST (120s) → FORCE_OFF | Short blip tolerable; sustained loss stops uncontrolled operation |

**HVAC**

| Actuator | Criticality | Power-Loss Mode | Comms-Loss Mode | Reasoning |
|---|---|---|---|---|
| Compressor relay | CRITICAL | FORCE_OFF | FORCE_OFF (30s) | Uncontrolled cycling risks equipment damage |
| Damper motor | STANDARD | FORCE_SAFE_POS (50% open) | HOLD_LAST (300s) → FORCE_SAFE_POS | Full-closed loses ventilation; full-open wastes energy; midpoint is the safe default |
| Fan motor | STANDARD | FORCE_OFF | HOLD_LAST (300s) → FORCE_OFF | — |

**Home/Building**

| Actuator | Criticality | Power-Loss Mode | Comms-Loss Mode | Reasoning |
|---|---|---|---|---|
| Electronic door lock | CRITICAL | **Fail-unlocked (regulatory, not purely engineering)** | HOLD_LAST | Fire/egress codes in most jurisdictions require electronic locks to fail unlocked — this direction must come from compliance/legal review per deployment region, not engineering default |
| Irrigation valve | OPERATIONAL | FORCE_OFF | HOLD_LAST (600s) → FORCE_OFF | Low safety stakes; favor water conservation |
| Light relay | OPERATIONAL | FORCE_OFF | HOLD_LAST (indefinite) | No safety implication either way |

**Process note:** for any life-safety-adjacent actuator (e.g. door
locks, egress-related dampers), the fail-safe direction must be signed
off by compliance/legal review for the deployment jurisdiction — this is
not solely an engineering decision and should not be defaulted by the
firmware team.

### Integration

Domain profiles supply their fail-safe table through the same vtable
pattern established in Section 6. The sensor validation gate (Section 9)
routes through this same mechanism — a validation failure is treated as
a trust-loss event and triggers the same fail-safe lookup as a comms or
power loss.

**Trade-offs:**
- `HOLD_LAST` on power loss requires local energy storage (battery/
  supercapacitor) on the actuator driver circuit — a hardware BOM
  decision, not a firmware-only one. Without it, `HOLD_LAST` silently
  degrades to de-energize.
- Comms-loss timeout values are a real tuning trade-off: too short
  causes nuisance fail-safe triggers on normal wireless retries; too
  long extends the unsafe window during a genuine comms failure. Values
  shown above are illustrative starting points, not field-validated
  defaults.

---

## 11. Requirements Traceability Matrix

Required if formal certification (IEC 61511/62443-class) is pursued for
any domain profile. Every safety requirement must map to its
implementation and its verifying test, with independent ownership at
each stage.

| Field | Purpose |
|---|---|
| Requirement ID | Unique, stable identifier (e.g. SR-001) |
| Requirement statement | Plain-language safety claim |
| Domain | Which profile(s) it applies to |
| Implementation | file:function where it is enforced |
| Test | Test file that verifies it |
| Verification status | Verified / Pending / Failed |

**Example rows:**

| Req ID | Requirement | Domain | Implementation | Test | Status |
|---|---|---|---|---|---|
| SR-001 | Chlorine dosing shall stop within 10s of comms loss | Water | `water_profile.c:water_failSafe()` | `test_water_failsafe.c` | Verified |
| SR-002 | Door lock shall fail unlocked on power loss | Home | `home_profile.c:home_failSafe()` | `test_home_failsafe.c` | Pending legal sign-off |
| SR-003 | Out-of-bounds sensor reading shall trigger fail-safe, never be used as valid | All | `sensor_validation.h:sensorValidate()` | `test_sensor_validation.c` | Verified |
| SR-004 | SAFETY_LOCKED rules shall be unmodifiable via field config portal | All | `config_portal.h` + `RuleEntry_t.rule_class` | `test_rule_locking.c` | Verified |

**Process rule:** requirement author, implementer, and verifier should be
different people/roles — self-verification undermines the independence
certification bodies expect. This matrix is a living document, updated
whenever a safety rule or actuator is added; an out-of-date matrix is a
certification-audit finding waiting to happen.

---

## 12. Flash Wear & RTC Fallback

**Flash wear (historian + NVS):** internal flash typically rated for
~100K erase cycles per sector. With a rotating historian (30-day window)
and periodic NVS writes (rule config, 90-day key rotation), lifetime must
be explicitly calculated per domain's expected write frequency:

```
sector writes/day × 365 × target_years < ~100K cycles
```

This calculation is domain-dependent (write-heavy industrial telemetry
vs infrequent home-automation writes) and must be done before field
deployment, not discovered after devices start failing at year 2-3.
Mitigations if the budget is tight: rely on LittleFS's built-in
wear-leveling, and coalesce writes into batches rather than per-record.

**RTC fallback:** audit-log and historian timestamps currently depend on
NTP. If NTP is unavailable (offline site, firewalled network), timestamp
integrity — and therefore forensic value of the audit log — degrades.
**Recommendation:** add a battery-backed RTC (e.g. DS3231, ~$1 BOM) to
the Hub. Sync RTC from NTP when available; fall back to RTC when not.
Typical RTC drift (~1 min/year) is acceptable for audit purposes.

---

## 13. Physical / Enclosure Requirements & Supply-Chain Hygiene

**Enclosure requirements (domain-dependent):**

| Domain | IP Rating | Tamper Switch | Operating Temp |
|---|---|---|---|
| Industrial | IP65+ | Mandatory | -20°C to 60°C |
| Agriculture | IP66/67 | Recommended | -10°C to 50°C |
| Home/Building | IP20 | Optional | 0°C to 40°C |

**CVE / SBOM tracking:** third-party SDKs and libraries (ESP RainMaker,
LittleFS, Arduino core, NimBLE, etc.) must be version-pinned and recorded
in a simple SBOM (library, version, source). In the absence of automated
dependency scanning for embedded toolchains, a **quarterly manual review**
against the NVD CVE database is the minimum acceptable process, with a
defined patch pipeline for anything flagged.

---

## 14. Summary — Status of All Open Items

| Item | Status |
|---|---|
| Requirements traceability matrix | Structure defined (Section 11); population is ongoing engineering work |
| Flash wear analysis | Method defined (Section 12); domain-specific numbers require field write-frequency data |
| RTC fallback | Recommended hardware addition (Section 12); not yet in BOM |
| Enclosure requirements | Tiered by domain (Section 13); specific part selection pending |
| CVE/SBOM process | Process defined (Section 13); tooling/ownership assignment pending |

No item in this document is claimed "fully resolved" — each has a
defined next action. This is intentional: the goal of this document is a
sound, extensible architecture with honestly-stated boundaries, not a
premature claim of completeness.

---

*Compiled from Layers 1–12. All sections approved by stakeholder through
the review process. This is the current architecture baseline (v2).*
