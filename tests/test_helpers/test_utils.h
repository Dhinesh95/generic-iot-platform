/**
 * @file test_utils.h
 * @brief Minimal test utilities for Phase 1 automated tests.
 *
 * This is NOT Unity — it is a lightweight assertion macro set
 * for tests that run on the host (not on target hardware).
 */

#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Assertion macros ---------- */

#define TEST_ASSERT(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while(0)

#define TEST_ASSERT_EQUAL(expected, actual) do { \
    if ((expected) != (actual)) { \
        fprintf(stderr, "FAIL: %s:%d: expected %d, got %d\n", \
                __FILE__, __LINE__, (int)(expected), (int)(actual)); \
        return 1; \
    } \
} while(0)

#define TEST_ASSERT_EQUAL_FLOAT(expected, actual, delta) do { \
    float _diff = (expected) - (actual); \
    if (_diff < 0) _diff = -_diff; \
    if (_diff > (delta)) { \
        fprintf(stderr, "FAIL: %s:%d: expected %.4f, got %.4f (delta=%.4f)\n", \
                __FILE__, __LINE__, (double)(expected), (double)(actual), (double)(delta)); \
        return 1; \
    } \
} while(0)

#define TEST_ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) { \
        fprintf(stderr, "FAIL: %s:%d: expected non-NULL\n", __FILE__, __LINE__); \
        return 1; \
    } \
} while(0)

#define TEST_ASSERT_NULL(ptr) do { \
    if ((ptr) != NULL) { \
        fprintf(stderr, "FAIL: %s:%d: expected NULL\n", __FILE__, __LINE__); \
        return 1; \
    } \
} while(0)

#define TEST_ASSERT_MEM_EQUAL(expected, actual, len) do { \
    if (memcmp((expected), (actual), (len)) != 0) { \
        fprintf(stderr, "FAIL: %s:%d: memory mismatch over %d bytes\n", \
                __FILE__, __LINE__, (int)(len)); \
        return 1; \
    } \
} while(0)

#define TEST_PASS() do { \
    return 0; \
} while(0)

/* ---------- Test runner helper ---------- */

#define RUN_TEST(test_func) do { \
    printf("  Running %s... ", #test_func); \
    int _result = test_func(); \
    if (_result == 0) { \
        printf("PASS\n"); \
        _passed++; \
    } else { \
        printf("FAIL\n"); \
        _failed++; \
    } \
    _total++; \
} while(0)

#define PRINT_TEST_SUMMARY() do { \
    printf("\n========================================\n"); \
    printf("Tests: %d total, %d passed, %d failed\n", _total, _passed, _failed); \
    printf("========================================\n"); \
    return (_failed > 0) ? 1 : 0; \
} while(0)

#endif /* TEST_UTILS_H */
