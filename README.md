# Generic IoT Platform

A three-tier IoT platform firmware (Edge → Gateway → Hub → Cloud) with 457 tests across 28 suites, covering Phases 1–18.1 of development.

## Architecture

```
┌──────────┐    RS-485    ┌──────────┐    LoRa/Zigbee    ┌──────────┐    MQTT    ┌─────────┐
│  Edge    │─────────────▶│ Gateway  │──────────────────▶│   Hub    │──────────▶│  Cloud  │
│  Node    │  Attested    │  Device  │  Authenticated    │  Server  │  TLS     │  Store  │
│ (PY32)   │  Sensor Data │ (ESP32)  │  Batched Frames   │ (ESP32)  │  Encrypted│(InfluxDB)│
└──────────┘              └──────────┘                   └──────────┘          └─────────┘
```

### Edge Tier (`firmware/edge/`)
- **Sensor drivers** — pluggable drivers for distance, temperature, humidity, light, soil moisture, pH, chlorine, etc.
- **Edge attestation** — HMAC-SHA256 attestation proving node identity to the Hub
- **Edge config** — per-node configuration and power profiling
- **RS-485 framing** — binary frame encoding for Edge↔Gateway communication

### Gateway Tier (`firmware/gateway/`)
- **Delta filter** — suppresses unchanged readings to reduce bandwidth
- **Stateful cache** — deduplication and staleness tracking
- **Batch forwarder** — coalesces multiple readings into authenticated batch frames
- **Gateway auth** — HMAC-signed batch frames proving Gateway identity

### Hub Tier (`firmware/hub/`)
- **Rule engine** — domain-profile-specific safety rules (Home lock, Agriculture irrigation, HVAC limits, Water Treatment chlorine/pH bounds)
- **Sensor validation** — per-metric bounds checking with domain-aware thresholds
- **Actuator failsafe** — emergency shutoff when safety thresholds are exceeded
- **Ingestion handler** — production pipeline: receive → attest → validate → evaluate → store → forward
- **Historian** — 30-day encrypted ring-buffer storage with AES-128-CCM at-rest encryption
- **Config portal** — HTTP-based device configuration interface
- **OTA session** — chunked firmware download with A/B flash partitions and rollback

### Cloud Tier (`cloud/`)
- **Telemetry store** — multi-tenant time-series data with per-tenant token authorization
- **Ingestion service** — MQTT-based cloud data ingestion pipeline
- **InfluxDB client** — line-protocol writer with tenant-scoped bucket isolation

### Shared Libraries (`firmware/shared/`)
| Module | Purpose |
|--------|---------|
| `attestation` | HMAC-SHA256 attestation primitives |
| `transport_encryption` | AES-128-CCM session encryption |
| `replay_protection` | Sliding-window replay attack prevention |
| `device_identity` | Versioned device identity with pending/confirm flow |
| `firmware_integrity` | Incremental HMAC firmware verification |
| `flash_partition` | Mockable A/B flash partition abstraction |
| `ota_manifest` | OTA manifest compatibility checking |
| `ota_canary` | Canary-percentage fleet rollout grouping |
| `rule_engine_core` | Domain-agnostic rule evaluation engine |
| `sensor_validation` | Metric bounds checking framework |
| `actuator_failsafe` | Emergency actuator shutdown |
| `audit_log` | Tamper-evident audit logging |
| `time_source` | Mockable time abstraction |
| `tamper_detect` | Physical tamper detection |
| `jtag_disable` | JTAG port lock on production devices |
| `power_profile` | Energy consumption tracking |
| `mqtt_client` | MQTT publish/subscribe |
| `lora_handler` | LoRa radio abstraction |
| `zigbee_handler` | Zigbee radio abstraction |
| `rs485_protocol` | RS-485 binary framing |
| `influxdb_client` | InfluxDB 2.x line-protocol client |

### Domain Profiles (`firmware/profiles/`)

| Profile | Metrics | Safety Rules |
|---------|---------|-------------|
| **Home** | Door lock, motion, light, temperature, humidity | SAFETY_LOCKED, temperature bounds, intrusion alert |
| **Agriculture** | Soil moisture, pH, light, temperature, wind speed | Irrigation scheduling, frost protection, UV warning |
| **HVAC** | Temperature, humidity, airflow, filter pressure | Compressor limits, freeze protection, pressure relief |
| **Water Treatment** | Chlorine, pH, turbidity, flow rate, pressure | Chlorine bounds, pH limits, turbidity max, pressure relief |

### Grafana Dashboards (`grafana/dashboards/`)
- **Node Overview** — live node count, per-node metric history, RSSI heatmap, fault frequency
- **Actuator Control** — actuator state, command history, failsafe events
- **Predictive Maintenance** — energy trends, degradation alerts, maintenance scheduling
- **Security & Audit** — security events, attestation failures, tamper detections, audit trail

## Project Structure

```
├── firmware/
│   ├── edge/              # Edge node firmware (PY32)
│   ├── gateway/           # Gateway firmware (ESP32)
│   ├── hub/               # Hub firmware (ESP32)
│   ├── shared/            # Cross-tier shared libraries
│   └── profiles/          # Domain-specific rule/config sets
│       ├── home/
│       ├── agriculture/
│       ├── hvac/
│       └── water_treatment/
├── cloud/                 # Cloud ingestion & telemetry
├── grafana/               # Dashboard provisioning & JSON
├── tests/                 # All test suites
│   ├── test_helpers/      # Shared test utilities
│   ├── test_attestation.c
│   ├── test_transport_encryption.c
│   ├── test_gateway.c
│   ├── test_edge_node.c
│   ├── test_full_pipeline_integration.c
│   ├── test_influxdb_client.c
│   ├── test_ota_session.c
│   └── ... (28 test files)
├── docs/
│   ├── architecture/      # Architecture design documents
│   └── traceability/      # Requirements matrix, flash wear budget
├── lib/mbedtls/           # Vendored mbedTLS crypto library
├── Makefile               # Native build & test orchestration
└── platformio.ini         # ESP32 PlatformIO build config
```

## Prerequisites

- **GCC** (C11) — for native test compilation
- **GNU Make** — build orchestration
- **PlatformIO** — for ESP32 firmware builds (optional, not needed for tests)
- **InfluxDB 2.x** (optional) — for cloud integration tests
- **Grafana** (optional) — for dashboard verification tests

## Building & Running Tests

### Full test suite (native, no hardware required)

```bash
make test-all
```

This compiles and runs all 28 test suites (457 tests) using the native GCC toolchain. No ESP32 hardware or InfluxDB/Grafana instances are required — all hardware and external services are mocked.

### Individual test suites

```bash
# Build and run a specific test
make build/test_attestation && ./build/test_attestation
make build/test_gateway && ./build/test_gateway
make build/test_edge_node && ./build/test_edge_node
make build/test_historian && ./build/test_historian
make build/test_ota_session && ./build/test_ota_session
make build/test_influxdb_client && ./build/test_influxdb_client
```

### Test suite overview (28 suites, 457 tests)

| Category | Suites | Tests | Description |
|----------|--------|-------|-------------|
| Hub/Cloud | 22 | 316 | Core crypto, rule engine, sensor validation, profiles, OTA, MQTT, cloud |
| Gateway | 1 | 49 | Gateway auth, delta filter, batch forwarding, stateful cache |
| Edge | 1 | 38 | Edge node, sensor drivers, attestation, power profiling |
| Integration | 3 | 35 | Full pipeline integration, ingestion handler, historian |
| **Total** | **28** | **457** | |

### ESP32 firmware builds (requires PlatformIO)

```bash
# Build for each domain profile
pio run -e esp32-home
pio run -e esp32-agriculture
pio run -e esp32-hvac
pio run -e esp32-water-treatment
```

## Security Model

- **Edge attestation** — HMAC-SHA256, per-node keys, Hub-side trust tracking with staleness expiry
- **Gateway authentication** — HMAC-signed batch frames, independent trust domain from Edge
- **Transport encryption** — AES-128-CCM session encryption (mbedTLS)
- **Replay protection** — 64-packet sliding window per session
- **OTA integrity** — Incremental HMAC-SHA256 across chunked transfers, A/B flash rollback
- **Data-at-rest** — Historian records encrypted with AES-128-CCM (separate key from transport)
- **Tenant isolation** — InfluxDB buckets scoped per TenantToken_t, no cross-tenant access possible

## CI

GitHub Actions runs the full test suite on every push to `master` or `develop`, and on all pull requests. See [`.github/workflows/ci.yml`](.github/workflows/ci.yml).

## License

Proprietary — Internal use only.
