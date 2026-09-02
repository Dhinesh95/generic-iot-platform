/**
 * @file test_replay_protection.c
 * @brief Tests for nonce + timestamp anti-replay protection.
 *
 * Architecture ref: Section 4, Baseline — T9 (replay attack).
 */

#include "test_helpers/test_utils.h"
#include "../firmware/shared/replay_protection.h"

/* ---------- Test: init ---------- */
static int test_replay_init(void)
{
    TEST_ASSERT(replay_init() == true);
    TEST_PASS();
}

/* ---------- Test: first frame accepted ---------- */
static int test_replay_first_frame(void)
{
    replay_init();

    ReplayCheckResult_t result = replay_check(1, 0, 1000);
    TEST_ASSERT(result == REPLAY_OK);
    TEST_PASS();
}

/* ---------- Test: fresh frames accepted ---------- */
static int test_replay_fresh_frames(void)
{
    replay_init();

    /* Send monotonically increasing nonces. */
    TEST_ASSERT(replay_check(1, 0, 1000) == REPLAY_OK);
    TEST_ASSERT(replay_check(1, 1, 2000) == REPLAY_OK);
    TEST_ASSERT(replay_check(1, 2, 3000) == REPLAY_OK);
    TEST_ASSERT(replay_check(1, 10, 4000) == REPLAY_OK);
    TEST_PASS();
}

/* ---------- Test: duplicate nonce rejected ---------- */
static int test_replay_duplicate(void)
{
    replay_init();

    TEST_ASSERT(replay_check(1, 5, 1000) == REPLAY_OK);
    TEST_ASSERT(replay_check(1, 5, 2000) == REPLAY_ERR_DUPLICATE);
    TEST_PASS();
}

/* ---------- Test: old nonce outside window rejected ---------- */
static int test_replay_old_nonce(void)
{
    replay_init();

    /* Advance well past the window. */
    TEST_ASSERT(replay_check(1, 0, 1000) == REPLAY_OK);
    TEST_ASSERT(replay_check(1, 100, 2000) == REPLAY_OK);

    /* Nonce 0 is now outside the 64-entry window. */
    TEST_ASSERT(replay_check(1, 0, 3000) == REPLAY_ERR_DUPLICATE);
    TEST_PASS();
}

/* ---------- Test: nonce within window but not seen accepted ---------- */
static int test_replay_within_window(void)
{
    replay_init();

    /* Send nonces 0, 2, 4 (skipping 1 and 3). */
    TEST_ASSERT(replay_check(1, 0, 1000) == REPLAY_OK);
    TEST_ASSERT(replay_check(1, 2, 2000) == REPLAY_OK);
    TEST_ASSERT(replay_check(1, 4, 3000) == REPLAY_OK);

    /* Nonce 1 was never sent — it should be accepted. */
    TEST_ASSERT(replay_check(1, 1, 4000) == REPLAY_OK);

    /* But nonce 1 was just seen — sending it again should be rejected. */
    TEST_ASSERT(replay_check(1, 1, 5000) == REPLAY_ERR_DUPLICATE);
    TEST_PASS();
}

/* ---------- Test: timestamp drift rejected ---------- */
static int test_replay_timestamp_drift(void)
{
    replay_init();

    /* First frame sets the baseline timestamp. */
    TEST_ASSERT(replay_check(1, 0, 10000) == REPLAY_OK);

    /* Second frame: drift of 5000ms from baseline (within 10s limit). */
    TEST_ASSERT(replay_check(1, 1, 15000) == REPLAY_OK);
    /* Now highest_timestamp_ms = 15000. */

    /* Third frame: drift of 12000ms from the LAST timestamp (15000). */
    TEST_ASSERT(replay_check(1, 2, 27001) == REPLAY_ERR_STALE_TIMESTAMP);

    /* But drift of 9999ms from last timestamp is OK. */
    TEST_ASSERT(replay_check(1, 3, 24999) == REPLAY_OK);
    TEST_PASS();
}

/* ---------- Test: separate nodes tracked independently ---------- */
static int test_replay_separate_nodes(void)
{
    replay_init();

    /* Node 1 sends nonce 0. */
    TEST_ASSERT(replay_check(1, 0, 1000) == REPLAY_OK);
    /* Node 2 also sends nonce 0 — should be accepted (separate state). */
    TEST_ASSERT(replay_check(2, 0, 1000) == REPLAY_OK);
    /* Node 1 duplicate. */
    TEST_ASSERT(replay_check(1, 0, 1500) == REPLAY_ERR_DUPLICATE);
    /* Node 2 fresh. */
    TEST_ASSERT(replay_check(2, 1, 2000) == REPLAY_OK);
    TEST_PASS();
}

/* ---------- Test: reset node ---------- */
static int test_replay_reset_node(void)
{
    replay_init();

    TEST_ASSERT(replay_check(1, 0, 1000) == REPLAY_OK);
    TEST_ASSERT(replay_check(1, 0, 2000) == REPLAY_ERR_DUPLICATE);

    replay_reset_node(1);

    /* After reset, nonce 0 should be accepted again. */
    TEST_ASSERT(replay_check(1, 0, 3000) == REPLAY_OK);
    TEST_PASS();
}

/* ---------- Test: reset all ---------- */
static int test_replay_reset_all(void)
{
    replay_init();

    TEST_ASSERT(replay_check(1, 0, 1000) == REPLAY_OK);
    TEST_ASSERT(replay_check(2, 0, 1000) == REPLAY_OK);

    replay_reset_all();

    TEST_ASSERT(replay_check(1, 0, 2000) == REPLAY_OK);
    TEST_ASSERT(replay_check(2, 0, 2000) == REPLAY_OK);
    TEST_PASS();
}

/* ---------- Main ---------- */
int main(void)
{
    int _total = 0, _passed = 0, _failed = 0;

    printf("=== test_replay_protection ===\n");
    RUN_TEST(test_replay_init);
    RUN_TEST(test_replay_first_frame);
    RUN_TEST(test_replay_fresh_frames);
    RUN_TEST(test_replay_duplicate);
    RUN_TEST(test_replay_old_nonce);
    RUN_TEST(test_replay_within_window);
    RUN_TEST(test_replay_timestamp_drift);
    RUN_TEST(test_replay_separate_nodes);
    RUN_TEST(test_replay_reset_node);
    RUN_TEST(test_replay_reset_all);

    PRINT_TEST_SUMMARY();
}
