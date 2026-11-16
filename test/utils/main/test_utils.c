#include <stdint.h>
#include <string.h>
#include <sys/time.h>

#include "unity.h"
#include "utils.h"

#define BUFFER_LEN 8U

static char s_buffer[BUFFER_LEN];

void setUp(void)
{
    memset(s_buffer, 'x', sizeof(s_buffer));
}

void tearDown(void)
{
}

TEST_CASE("test_utils_bool_text_returns_json_literals", "[utils]")
{
    TEST_ASSERT_EQUAL_STRING("true", utils_bool_text(true));
    TEST_ASSERT_EQUAL_STRING("false", utils_bool_text(false));
}

TEST_CASE("test_utils_snprintf_fitting_text_returns_ok", "[utils]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, utils_snprintf(s_buffer, sizeof(s_buffer), "/%s/%d", "ab", 7));
    TEST_ASSERT_EQUAL_STRING("/ab/7", s_buffer);
}

TEST_CASE("test_utils_snprintf_text_filling_the_buffer_exactly_returns_ok", "[utils]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, utils_snprintf(s_buffer, sizeof(s_buffer), "%s", "1234567"));
    TEST_ASSERT_EQUAL_STRING("1234567", s_buffer);
}

TEST_CASE("test_utils_snprintf_text_without_room_for_terminator_returns_invalid_size", "[utils]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_SIZE,
                            utils_snprintf(s_buffer, sizeof(s_buffer), "%s", "12345678"));
    TEST_ASSERT_EQUAL_UINT32(BUFFER_LEN - 1U,
                             (uint32_t)strlen(s_buffer)); /* Truncated, terminated. */
}

TEST_CASE("test_utils_snprintf_empty_text_returns_invalid_size", "[utils]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_SIZE,
                            utils_snprintf(s_buffer, sizeof(s_buffer), "%s", ""));
}

TEST_CASE("test_utils_crc32_matches_standard_check_value", "[utils]")
{
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926U, utils_crc32("123456789", 9U));
}

TEST_CASE("test_utils_crc32_empty_input_returns_zero", "[utils]")
{
    TEST_ASSERT_EQUAL_HEX32(0U, utils_crc32("", 0U));
}

TEST_CASE("test_utils_crc32_changes_when_a_bit_flips", "[utils]")
{
    uint8_t data[4] = {1U, 2U, 3U, 4U};
    const uint32_t before = utils_crc32(data, sizeof(data));
    data[2] ^= 0x01U;
    TEST_ASSERT_NOT_EQUAL_HEX32(before, utils_crc32(data, sizeof(data)));
}

TEST_CASE("test_utils_epoch_ms_follows_the_system_clock", "[utils]")
{
    const struct timeval fixed = {.tv_sec = 1730660000, .tv_usec = 250000};
    TEST_ASSERT_EQUAL_INT32(0, settimeofday(&fixed, NULL));

    const int64_t now = utils_epoch_ms();

    TEST_ASSERT_GREATER_OR_EQUAL_INT64(1730660000250LL, now);
    TEST_ASSERT_LESS_THAN_INT64(1730660001250LL, now);
}
