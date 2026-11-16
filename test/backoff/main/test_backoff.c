#include <stdint.h>

#include "backoff.h"
#include "unity.h"

#define INITIAL_MS 1000U
#define MAX_MS     30000U

void setUp(void)
{
}

void tearDown(void)
{
}

TEST_CASE("test_backoff_first_retry_returns_initial_delay", "[backoff]")
{
    TEST_ASSERT_EQUAL_UINT32(INITIAL_MS, backoff_next_delay_ms(0U, INITIAL_MS, MAX_MS));
}

TEST_CASE("test_backoff_next_retry_doubles_previous_delay", "[backoff]")
{
    TEST_ASSERT_EQUAL_UINT32(2000U, backoff_next_delay_ms(1000U, INITIAL_MS, MAX_MS));
    TEST_ASSERT_EQUAL_UINT32(4000U, backoff_next_delay_ms(2000U, INITIAL_MS, MAX_MS));
}

TEST_CASE("test_backoff_delay_reaches_ceiling_saturates_at_max", "[backoff]")
{
    uint32_t delay_ms = 0U;
    for (uint32_t i = 0U; i < 10U; i++) {
        delay_ms = backoff_next_delay_ms(delay_ms, INITIAL_MS, MAX_MS);
    }
    TEST_ASSERT_EQUAL_UINT32(MAX_MS, delay_ms);
    TEST_ASSERT_EQUAL_UINT32(MAX_MS, backoff_next_delay_ms(delay_ms, INITIAL_MS, MAX_MS));
}

TEST_CASE("test_backoff_previous_above_max_returns_max", "[backoff]")
{
    TEST_ASSERT_EQUAL_UINT32(MAX_MS, backoff_next_delay_ms(MAX_MS * 4U, INITIAL_MS, MAX_MS));
}

TEST_CASE("test_backoff_huge_ceiling_does_not_overflow", "[backoff]")
{
    const uint32_t half = UINT32_MAX / 2U;

    TEST_ASSERT_EQUAL_UINT32(half * 2U, backoff_next_delay_ms(half, 1U, UINT32_MAX));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, backoff_next_delay_ms(half + 1U, 1U, UINT32_MAX));
}

TEST_CASE("test_backoff_initial_equal_to_max_always_returns_max", "[backoff]")
{
    TEST_ASSERT_EQUAL_UINT32(MAX_MS, backoff_next_delay_ms(0U, MAX_MS, MAX_MS));
    TEST_ASSERT_EQUAL_UINT32(MAX_MS, backoff_next_delay_ms(MAX_MS, MAX_MS, MAX_MS));
}
