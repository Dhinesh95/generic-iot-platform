# Makefile for Phase 1+5 host-based unit testing.
#
# This compiles all shared + profile + hub sources with the native
# compiler and runs each test binary independently.
#
# mbedTLS 2.28 is built from source in lib/mbedtls/ for native tests.
#
# Usage:
#   make test          — build and run all tests
#   make test-verbose  — build and run with verbose output
#   make clean         — remove build artifacts

CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -DPROFILE_HOME -DMBEDTLS_CONFIG_FILE='"mbedtls/config.h"' -include limits.h
INCLUDES = -Ifirmware/shared -Ifirmware/hub -Ifirmware/profiles/home -Ifirmware/profiles/agriculture -Ifirmware/profiles/hvac -Ifirmware/profiles/water_treatment -Itests -Ilib/mbedtls/include -Ilib/mbedtls/library
BUILD_DIR = build
TEST_DIR = tests

# mbedTLS library (built from source)
MBEDTLS_SRC_DIR = lib/mbedtls/library
MBEDTLS_SRCS = $(MBEDTLS_SRC_DIR)/sha256.c \
               $(MBEDTLS_SRC_DIR)/aes.c \
               $(MBEDTLS_SRC_DIR)/md.c \
               $(MBEDTLS_SRC_DIR)/cipher.c \
               $(MBEDTLS_SRC_DIR)/cipher_wrap.c \
               $(MBEDTLS_SRC_DIR)/constant_time.c \
               $(MBEDTLS_SRC_DIR)/platform_util.c \
               $(MBEDTLS_SRC_DIR)/oid.c \
               $(MBEDTLS_SRC_DIR)/ccm.c
MBEDTLS_LIB = $(BUILD_DIR)/libmbedtls.a

# Shared source files (core domain-agnostic)
SHARED_SRCS = \
	firmware/shared/audit_log.c \
	firmware/shared/attestation.c \
	firmware/shared/time_source.c \
	firmware/shared/device_identity.c \
	firmware/shared/ota_manifest.c \
	firmware/shared/ota_canary.c \
	firmware/shared/transport_encryption.c \
	firmware/shared/replay_protection.c \
	firmware/shared/firmware_integrity.c \
	firmware/shared/rule_engine_core.c \
	firmware/shared/sensor_validation.c \
	firmware/shared/actuator_failsafe.c \
	firmware/shared/rs485_protocol.c \
	firmware/shared/zigbee_handler.c \
	firmware/shared/mqtt_client.c \
	firmware/shared/power_profile.c \
	firmware/shared/lora_handler.c \
	firmware/shared/tamper_detect.c \
	firmware/shared/jtag_disable.c \
	firmware/shared/flash_partition.c \
	firmware/shared/ota_session.c \
	firmware/shared/influxdb_client.c

# Hub source
HUB_SRCS = \
	firmware/hub/config_portal.c \
	firmware/hub/ingestion_handler.c \
	firmware/hub/historian.c

# Cloud ingestion service (native/Linux-side, not embedded)
CLOUD_SRCS = \
	cloud/telemetry_store.c \
	cloud/ingestion_service.c

# Home profile sources
HOME_PROFILE_SRCS = \
	firmware/profiles/home/home_rules.c \
	firmware/profiles/home/home_validation_bounds.c \
	firmware/profiles/home/home_failsafe.c

# Agriculture profile sources
AGRI_PROFILE_SRCS = \
	firmware/profiles/agriculture/agriculture_rules.c \
	firmware/profiles/agriculture/agriculture_validation_bounds.c \
	firmware/profiles/agriculture/agriculture_failsafe.c

# HVAC profile sources
HVAC_PROFILE_SRCS = \
	firmware/profiles/hvac/hvac_rules.c \
	firmware/profiles/hvac/hvac_validation_bounds.c \
	firmware/profiles/hvac/hvac_failsafe.c

# Water Treatment profile sources
WATER_PROFILE_SRCS = \
	firmware/profiles/water_treatment/water_rules.c \
	firmware/profiles/water_treatment/water_validation_bounds.c \
	firmware/profiles/water_treatment/water_failsafe.c

# Gateway source files (Tier 2 — fog computing, domain-agnostic)
GATEWAY_SRCS = \
	firmware/gateway/stateful_cache.c \
	firmware/gateway/delta_filter.c \
	firmware/gateway/batch_forwarder.c \
	firmware/gateway/gateway_auth.c

# Edge Node source files (Tier 1 — sensor driver registry, domain-agnostic)
EDGE_SRCS = \
	firmware/edge/sensor_driver.c \
	firmware/edge/edge_config.c \
	firmware/edge/edge_node.c \
	firmware/edge/edge_attestation.c

# All source files needed for Hub/cloud tests
# Note: includes GATEWAY_SRCS because ingestion_handler.c depends on gateway_auth
ALL_SRCS = $(SHARED_SRCS) $(HUB_SRCS) $(CLOUD_SRCS) $(HOME_PROFILE_SRCS) $(AGRI_PROFILE_SRCS) $(HVAC_PROFILE_SRCS) $(WATER_PROFILE_SRCS) $(GATEWAY_SRCS)

# All source files needed for Gateway tests (Gateway + shared, no Hub/profiles)
GATEWAY_ALL_SRCS = $(SHARED_SRCS) $(GATEWAY_SRCS)

# All source files needed for Edge Node tests (Edge + shared, no Hub/profiles)
EDGE_ALL_SRCS = $(SHARED_SRCS) $(EDGE_SRCS)

# All source files needed for full integration test (all tiers)
FULL_ALL_SRCS = $(SHARED_SRCS) $(HUB_SRCS) $(CLOUD_SRCS) $(HOME_PROFILE_SRCS) $(AGRI_PROFILE_SRCS) $(HVAC_PROFILE_SRCS) $(WATER_PROFILE_SRCS) $(GATEWAY_SRCS) $(EDGE_SRCS)

# Test files (Hub/cloud tests)
TESTS = \
	test_attestation \
	test_transport_encryption \
	test_replay_protection \
	test_rule_engine_core \
	test_rule_locking \
	test_sensor_validation \
	test_actuator_failsafe \
	test_door_lock_failsafe_config \
	test_config_portal_auth \
	test_audit_log_persistence \
	test_home_profile \
	test_firmware_integrity \
	test_agriculture_profile \
	test_hvac_profile \
	test_water_treatment_profile \
	test_crypto_interop \
	test_mqtt_client_integration \
	test_enhanced_security \
	test_ota_fleet \
	test_cloud_ingestion \
	test_time_source \
	test_ota_session \
	test_influxdb_client

# Gateway test files
GATEWAY_TESTS = \
	test_gateway

# Edge Node test files
EDGE_TESTS = \
	test_edge_node

# Integration test files (full pipeline)
INTEGRATION_TESTS = \
	test_full_pipeline_integration \
	test_ingestion_handler \
	test_historian

.PHONY: all test test-gateway test-edge test-integration test-all test-verbose clean

all: test

# Build mbedTLS static library
$(MBEDTLS_LIB): $(MBEDTLS_SRCS) | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $(INCLUDES) $(MBEDTLS_SRCS) -I$(MBEDTLS_SRC_DIR)
	ar rcs $@ sha256.o aes.o md.o cipher.o cipher_wrap.o constant_time.o platform_util.o oid.o ccm.o
	rm -f sha256.o aes.o md.o cipher.o cipher_wrap.o constant_time.o platform_util.o oid.o ccm.o

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Build each Hub/cloud test binary (excluding Gateway tests)
$(BUILD_DIR)/test_attestation: $(TEST_DIR)/test_attestation.c $(ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(ALL_SRCS) -L$(BUILD_DIR) -lmbedtls
$(BUILD_DIR)/test_transport_encryption: $(TEST_DIR)/test_transport_encryption.c $(ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(ALL_SRCS) -L$(BUILD_DIR) -lmbedtls
$(BUILD_DIR)/test_replay_protection: $(TEST_DIR)/test_replay_protection.c $(ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(ALL_SRCS) -L$(BUILD_DIR) -lmbedtls
$(BUILD_DIR)/test_rule_engine_core: $(TEST_DIR)/test_rule_engine_core.c $(ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(ALL_SRCS) -L$(BUILD_DIR) -lmbedtls
$(BUILD_DIR)/test_rule_locking: $(TEST_DIR)/test_rule_locking.c $(ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(ALL_SRCS) -L$(BUILD_DIR) -lmbedtls
$(BUILD_DIR)/test_sensor_validation: $(TEST_DIR)/test_sensor_validation.c $(ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(ALL_SRCS) -L$(BUILD_DIR) -lmbedtls
$(BUILD_DIR)/test_actuator_failsafe: $(TEST_DIR)/test_actuator_failsafe.c $(ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(ALL_SRCS) -L$(BUILD_DIR) -lmbedtls
$(BUILD_DIR)/test_door_lock_failsafe_config: $(TEST_DIR)/test_door_lock_failsafe_config.c $(ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(ALL_SRCS) -L$(BUILD_DIR) -lmbedtls
$(BUILD_DIR)/test_config_portal_auth: $(TEST_DIR)/test_config_portal_auth.c $(ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(ALL_SRCS) -L$(BUILD_DIR) -lmbedtls
$(BUILD_DIR)/test_audit_log_persistence: $(TEST_DIR)/test_audit_log_persistence.c $(ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(ALL_SRCS) -L$(BUILD_DIR) -lmbedtls
$(BUILD_DIR)/test_home_profile: $(TEST_DIR)/test_home_profile.c $(ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(ALL_SRCS) -L$(BUILD_DIR) -lmbedtls
$(BUILD_DIR)/test_firmware_integrity: $(TEST_DIR)/test_firmware_integrity.c $(ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(ALL_SRCS) -L$(BUILD_DIR) -lmbedtls
$(BUILD_DIR)/test_agriculture_profile: $(TEST_DIR)/test_agriculture_profile.c $(ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(ALL_SRCS) -L$(BUILD_DIR) -lmbedtls
$(BUILD_DIR)/test_hvac_profile: $(TEST_DIR)/test_hvac_profile.c $(ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(ALL_SRCS) -L$(BUILD_DIR) -lmbedtls
$(BUILD_DIR)/test_water_treatment_profile: $(TEST_DIR)/test_water_treatment_profile.c $(ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(ALL_SRCS) -L$(BUILD_DIR) -lmbedtls
$(BUILD_DIR)/test_crypto_interop: $(TEST_DIR)/test_crypto_interop.c $(ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(ALL_SRCS) -L$(BUILD_DIR) -lmbedtls
$(BUILD_DIR)/test_mqtt_client_integration: $(TEST_DIR)/test_mqtt_client_integration.c $(ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(ALL_SRCS) -L$(BUILD_DIR) -lmbedtls
$(BUILD_DIR)/test_enhanced_security: $(TEST_DIR)/test_enhanced_security.c $(ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(ALL_SRCS) -L$(BUILD_DIR) -lmbedtls
$(BUILD_DIR)/test_ota_fleet: $(TEST_DIR)/test_ota_fleet.c $(ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(ALL_SRCS) -L$(BUILD_DIR) -lmbedtls
$(BUILD_DIR)/test_cloud_ingestion: $(TEST_DIR)/test_cloud_ingestion.c $(ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(ALL_SRCS) -L$(BUILD_DIR) -lmbedtls
$(BUILD_DIR)/test_time_source: $(TEST_DIR)/test_time_source.c $(ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(ALL_SRCS) -L$(BUILD_DIR) -lmbedtls

# Build Gateway test binary (Gateway sources + shared, no Hub/profiles)
$(BUILD_DIR)/test_gateway: $(TEST_DIR)/test_gateway.c $(GATEWAY_ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(GATEWAY_ALL_SRCS) -L$(BUILD_DIR) -lmbedtls

# Build Edge Node test binary (Edge sources + shared, no Hub/profiles)
$(BUILD_DIR)/test_edge_node: $(TEST_DIR)/test_edge_node.c $(EDGE_ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(EDGE_ALL_SRCS) -L$(BUILD_DIR) -lmbedtls

# Build full integration test binary (all tiers)
$(BUILD_DIR)/test_full_pipeline_integration: $(TEST_DIR)/test_full_pipeline_integration.c $(FULL_ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(FULL_ALL_SRCS) -L$(BUILD_DIR) -lmbedtls -lm

# Build ingestion handler test binary (Hub + Gateway + Edge + profiles)
$(BUILD_DIR)/test_ingestion_handler: $(TEST_DIR)/test_ingestion_handler.c $(FULL_ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(FULL_ALL_SRCS) -L$(BUILD_DIR) -lmbedtls -lm

# Build historian test binary (Hub + Gateway + Edge + profiles)
$(BUILD_DIR)/test_historian: $(TEST_DIR)/test_historian.c $(FULL_ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(FULL_ALL_SRCS) -L$(BUILD_DIR) -lmbedtls -lm

# Build OTA session test binary (shared modules)
$(BUILD_DIR)/test_ota_session: $(TEST_DIR)/test_ota_session.c $(ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(ALL_SRCS) -L$(BUILD_DIR) -lmbedtls -lm

# Build InfluxDB client test binary (shared modules)
$(BUILD_DIR)/test_influxdb_client: $(TEST_DIR)/test_influxdb_client.c $(ALL_SRCS) $(MBEDTLS_LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $< $(ALL_SRCS) -L$(BUILD_DIR) -lmbedtls -lm

# Run Hub/Cloud tests
test: $(addprefix $(BUILD_DIR)/,$(TESTS))
	@echo ""
	@echo "========================================"
	@echo "Running Hub/Cloud tests..."
	@echo "========================================"
	@failed=0; passed=0; total=0; \
	for test in $(TESTS); do \
		echo ""; \
		./$(BUILD_DIR)/$$test; \
		if [ $$? -eq 0 ]; then passed=$$((passed + 1)); else failed=$$((failed + 1)); fi; \
		total=$$((total + 1)); \
	done; \
	echo ""; \
	echo "========================================"; \
	echo "Test suites: $$total total, $$passed passed, $$failed failed"; \
	echo "========================================"; \
	if [ $$failed -gt 0 ]; then exit 1; fi

# Run Gateway tests
test-gateway: $(addprefix $(BUILD_DIR)/,$(GATEWAY_TESTS))
	@echo ""
	@echo "========================================"
	@echo "Running Gateway tests..."
	@echo "========================================"
	@failed=0; passed=0; total=0; \
	for test in $(GATEWAY_TESTS); do \
		echo ""; \
		./$(BUILD_DIR)/$$test; \
		if [ $$? -eq 0 ]; then passed=$$((passed + 1)); else failed=$$((failed + 1)); fi; \
		total=$$((total + 1)); \
	done; \
	echo ""; \
	echo "========================================"; \
	echo "Gateway suites: $$total total, $$passed passed, $$failed failed"; \
	echo "========================================"; \
	if [ $$failed -gt 0 ]; then exit 1; fi

# Run Edge Node tests
test-edge: $(addprefix $(BUILD_DIR)/,$(EDGE_TESTS))
	@echo ""
	@echo "========================================"
	@echo "Running Edge Node tests..."
	@echo "========================================"
	@failed=0; passed=0; total=0; \
	for test in $(EDGE_TESTS); do \
		echo ""; \
		./$(BUILD_DIR)/$$test; \
		if [ $$? -eq 0 ]; then passed=$$((passed + 1)); else failed=$$((failed + 1)); fi; \
		total=$$((total + 1)); \
	done; \
	echo ""; \
	echo "========================================"; \
	echo "Edge suites: $$total total, $$passed passed, $$failed failed"; \
	echo "========================================"; \
	if [ $$failed -gt 0 ]; then exit 1; fi

# Run Integration tests
test-integration: $(addprefix $(BUILD_DIR)/,$(INTEGRATION_TESTS))
	@echo ""
	@echo "========================================"
	@echo "Running Full Pipeline Integration tests..."
	@echo "========================================"
	@failed=0; passed=0; total=0; \
	for test in $(INTEGRATION_TESTS); do \
		echo ""; \
		./$(BUILD_DIR)/$$test; \
		if [ $$? -eq 0 ]; then passed=$$((passed + 1)); else failed=$$((failed + 1)); fi; \
		total=$$((total + 1)); \
	done; \
	echo ""; \
	echo "========================================"; \
	echo "Integration suites: $$total total, $$passed passed, $$failed failed"; \
	echo "========================================"; \
	if [ $$failed -gt 0 ]; then exit 1; fi

# Run ALL tests (Hub + Gateway + Edge + Integration)
test-all: test test-gateway test-edge test-integration

test-verbose: test

clean:
	rm -rf $(BUILD_DIR)
