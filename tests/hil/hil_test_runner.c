/**
 * @file hil_test_runner.c
 * @brief HIL test runner implementation.
 */

#include "hil_test_runner.h"
#include <stdio.h>
#include <string.h>

static const HILHardwareBackend_t *s_hw = NULL;

/* ---------- API ---------- */

bool hil_init(const HILHardwareBackend_t *hw)
{
    s_hw = hw;
    return true;
}

bool hil_is_real_hardware(void)
{
    return (s_hw != NULL);
}

HILTestResult_t hil_run_single(HILTestContext_t *ctx, HILTestFunc_t test)
{
    if (!ctx || !test) return HIL_TEST_ERROR;

    ctx->hw = s_hw;

    printf("  Running %s... ", ctx->test_name);
    HILTestResult_t result = test(ctx);

    switch (result) {
        case HIL_TEST_PASS:  printf("PASS\n"); break;
        case HIL_TEST_FAIL:  printf("FAIL\n"); break;
        case HIL_TEST_SKIP:  printf("SKIP (hardware not available)\n"); break;
        case HIL_TEST_ERROR: printf("ERROR\n"); break;
    }

    return result;
}

uint8_t hil_run_suite(HILTestContext_t *ctx, const HILTestSuite_t *suite)
{
    if (!ctx || !suite) return 1;

    printf("\n=== HIL Suite: %s ===\n\n", suite->suite_name);

    uint8_t failures = 0;
    for (uint8_t i = 0; i < suite->test_count; i++) {
        strncpy(ctx->test_name, suite->tests[i].name, HIL_TEST_NAME_MAX - 1);
        ctx->test_name[HIL_TEST_NAME_MAX - 1] = '\0';

        HILTestResult_t result = hil_run_single(ctx, suite->tests[i].func);
        if (result == HIL_TEST_FAIL || result == HIL_TEST_ERROR) {
            failures++;
        }
    }

    printf("\n========================================\n");
    printf("  Suite: %s — %d/%d passed\n",
           suite->suite_name,
           suite->test_count - failures,
           suite->test_count);
    printf("========================================\n");

    return failures;
}

uint8_t hil_run_all(HILTestContext_t *ctx)
{
    if (!ctx) return 1;

    printf("\n=== HIL Test Runner ===\n");
    printf("Hardware: %s\n", s_hw ? "REAL" : "MOCK");
    printf("========================\n");

    /* Tests are registered via hil_run_suite calls from main(). */
    return 0;
}
