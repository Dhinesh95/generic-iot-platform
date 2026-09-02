# Threat Model Update — Phases 13–28

**Date:** September 2, 2026
**Original Threat Model:** Sections 4, 6, 7 of Architecture Document
**Update Scope:** New attack surfaces introduced by Phases 13–28

## Original Threats (T1–T9)

| ID | Threat | Mitigation | Status |
|----|--------|------------|--------|
| T1 | Rogue node spoofing | Edge attestation + Hub trust tracker | ✅ Mitigated |
| T2 | Wireless MITM | AES-128-CCM transport encryption | ✅ Mitigated |
| T3 | Data tampering in transit | HMAC-SHA256 frame integrity | ✅ Mitigated |
| T4 | Lateral movement | Per-node attestation keys | ✅ Mitigated |
| T5 | Denial of service | Replay protection (64-pkt window) | ✅ Mitigated |
| T6 | Unauthorized configuration | Config portal auth (Phase 5) | ✅ Mitigated |
| T7 | Firmware tampering | HMAC integrity check (Phase 3) | ✅ Mitigated |
| T8 | JTAG debugging | JTAG disable on production (Phase 3) | ✅ Mitigated |
| T9 | Replay attacks | Nonce + timestamp anti-replay | ✅ Mitigated |

## New Threats (T10–T17)

| ID | Threat | Attack Surface | Mitigation | Status |
|----|--------|---------------|------------|--------|
| T10 | Malicious OTA image | Phase 17: OTA transport | HMAC integrity + A/B rollback + confirm-before-commit | ✅ Mitigated |
| T11 | OTA manifest spoofing | Phase 7: manifest check | Domain/hw/version compatibility check before chunk acceptance | ✅ Mitigated |
| T12 | InfluxDB tenant escape | Phase 18.1: InfluxDB client | TenantToken_t enforcement — bucket derived from token, not caller | ✅ Mitigated |
| T13 | Grafana data leakage | Phase 18: Grafana dashboards | Tenant-scoped buckets, per-tenant tokens | ✅ Mitigated |
| T14 | NULL bypass vulnerability | Phases 14.2, 15.2: security gates | Explicit sentinels, full codebase audit | ✅ Mitigated |
| T15 | Cloud message injection | Phase 8: MQTT ingestion | MQTT auth + replay protection + HMAC | ✅ Mitigated |
| T16 | Flash wear DoS | Phase 16.1: historian writes | Batch flush + wear budget analysis | ⚠️ Partial |
| T17 | Clock manipulation | Phase 9: RTC tamper | Tamper detection + timestamp drift tolerance | ✅ Mitigated |
| T18 | Rule engine silent failure | Phase 19: safety monitor | Independent watchdog + heartbeat verification | ✅ Mitigated |
| T19 | Fault cascade | Phase 20: fault tree | Graceful degradation matrix | ✅ Mitigated |
| T20 | Boot integrity compromise | Phase 21: verified boot | HMAC-verified boot chain + rollback | ✅ Mitigated |
| T21 | Diagnostic data exposure | Phase 22: diagnostics | Structured output, no raw memory dumps | ✅ Mitigated |

## Attack Surface Analysis by Tier

### Edge Tier
- **Entry points:** RS-485 (Gateway), sensor inputs, JTAG (disabled)
- **Threats:** T1 (rogue node), T2 (wireless MITM), T8 (JTAG), T21 (power state)
- **Mitigations:** Attestation, transport encryption, JTAG disable, power state management

### Gateway Tier
- **Entry points:** LoRa/Zigbee (Edge), MQTT (Hub)
- **Threats:** T3 (data tampering), T5 (DoS), T14 (NULL bypass)
- **Mitigations:** HMAC batch frames, replay protection, explicit sentinels

### Hub Tier
- **Entry points:** LoRa/Zigbee (Gateway), MQTT (Cloud), HTTP (config portal)
- **Threats:** T1 (rogue Edge), T12 (tenant escape), T15 (cloud injection), T18 (rule engine stall)
- **Mitigations:** Per-Edge trust tracking, TenantToken_t, MQTT auth, safety monitor

### Cloud Tier
- **Entry points:** MQTT (Hub), HTTP (InfluxDB/Grafana)
- **Threats:** T12 (InfluxDB tenant escape), T13 (Grafana data leak), T15 (message injection)
- **Mitigations:** TenantToken_t, tenant-scoped buckets, MQTT auth

## Penetration Test Scenarios

### PT-01: Rogue Edge Node Injection
- Provision Edge Node with wrong key
- Connect to Gateway
- **Expected:** Hub rejects data at ingestion gate
- **Verify:** Audit log records rejection, rule engine never sees data

### PT-02: Tampered Gateway Batch
- Craft batch frame with corrupted HMAC
- Send to Hub via LoRa
- **Expected:** Hub rejects frame
- **Verify:** No entries reach rule engine

### PT-03: OTA Image Tampering
- Modify one byte of firmware image
- Attempt OTA transfer
- **Expected:** Integrity check fails
- **Verify:** No version change committed, rollback triggered

### PT-04: Cross-Tenant Data Access
- Use tenant A token to query tenant B bucket
- **Expected:** Unauthorized error
- **Verify:** No data returned

### PT-05: Replay Attack
- Capture valid attestation response
- Re-send after 30 seconds
- **Expected:** Replay detection rejects stale frame
- **Verify:** Nonce window updated, timestamp drift detected

### PT-06: NULL Bypass Audit
- Search codebase for NULL-means-skip patterns
- **Expected:** All security functions use explicit sentinels
- **Verify:** No function accepts NULL to skip auth

### PT-07: Rule Engine Stall
- Simulate rule engine hang (timeout in test)
- **Expected:** Safety monitor triggers failsafe
- **Verify:** All actuators go to safe state within 500ms

### PT-08: Fault Cascade
- Simulate radio loss + cloud unreachable + low memory
- **Expected:** System degrades to EMERGENCY level
- **Verify:** Only safety rules active, all else suspended

### PT-09: Boot Integrity
- Corrupt flash image in inactive slot
- Attempt boot from corrupted slot
- **Expected:** Verified boot rejects image, rollback to previous slot
- **Verify:** System boots into known-good image

### PT-10: Flash Wear DoS
- Write historian records at maximum rate for extended period
- **Expected:** Batch flush prevents flash wear budget overrun
- **Verify:** Flash wear within acceptable limits per Phase 10 analysis
