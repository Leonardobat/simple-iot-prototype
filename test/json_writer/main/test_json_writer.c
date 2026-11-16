#include <stdint.h>
#include <string.h>

#include "json_writer.h"
#include "unity.h"

#define BUFFER_LEN         32U
#define CATALOG_BUFFER_LEN 64U

static char s_buffer[BUFFER_LEN];
static json_writer_t s_writer;

void setUp(void)
{
    memset(s_buffer, 'x', sizeof(s_buffer));
}

void tearDown(void)
{
}

TEST_CASE("test_json_writer_no_members_writes_empty_object", "[json_writer]")
{
    json_writer_begin(&s_writer, s_buffer, sizeof(s_buffer));

    TEST_ASSERT_EQUAL_INT32(ESP_OK, json_writer_end(&s_writer));
    TEST_ASSERT_EQUAL_STRING("{}", s_buffer);
}

TEST_CASE("test_json_writer_members_are_comma_separated", "[json_writer]")
{
    json_writer_begin(&s_writer, s_buffer, sizeof(s_buffer));
    json_writer_add(&s_writer, "\"%s\":%d", "a", 1);
    json_writer_add(&s_writer, "\"%s\":%s", "b", "true");

    TEST_ASSERT_EQUAL_INT32(ESP_OK, json_writer_end(&s_writer));
    TEST_ASSERT_EQUAL_STRING("{\"a\":1,\"b\":true}", s_buffer);
}

TEST_CASE("test_json_writer_exact_fit_returns_ok", "[json_writer]")
{
    char exact[sizeof("{\"a\":1}")] = {0};
    json_writer_begin(&s_writer, exact, sizeof(exact));
    json_writer_add(&s_writer, "\"a\":%d", 1);

    TEST_ASSERT_EQUAL_INT32(ESP_OK, json_writer_end(&s_writer));
    TEST_ASSERT_EQUAL_STRING("{\"a\":1}", exact);
}

TEST_CASE("test_json_writer_overflow_returns_invalid_size_and_stays_terminated", "[json_writer]")
{
    char small[8] = {0};
    json_writer_begin(&s_writer, small, sizeof(small));
    json_writer_add(&s_writer, "\"%s\":%d", "too_long_key", 1);

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_SIZE, json_writer_end(&s_writer));
    TEST_ASSERT_LESS_THAN_UINT32(sizeof(small), (uint32_t)strlen(small));
}

TEST_CASE("test_json_writer_no_room_for_closing_brace_returns_invalid_size", "[json_writer]")
{
    char small[sizeof("{\"a\":1}") - 1U] = {0};
    json_writer_begin(&s_writer, small, sizeof(small));
    json_writer_add(&s_writer, "\"a\":%d", 1);

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_SIZE, json_writer_end(&s_writer));
}

TEST_CASE("test_json_writer_member_after_overflow_is_skipped", "[json_writer]")
{
    char small[8] = {0};
    json_writer_begin(&s_writer, small, sizeof(small));
    json_writer_add(&s_writer, "\"%s\":%d", "too_long_key", 1);
    json_writer_add(&s_writer, "\"b\":%d", 2);

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_SIZE, json_writer_end(&s_writer));
    TEST_ASSERT_NULL(strstr(small, "\"b\""));
}

TEST_CASE("test_json_writer_catalog_item_writes_type_unit_and_simulated", "[json_writer]")
{
    char big[CATALOG_BUFFER_LEN] = {0};
    json_writer_begin(&s_writer, big, sizeof(big));
    json_writer_add_catalog_item(&s_writer, &(json_catalog_item_t){"a", "t", "u", true});

    TEST_ASSERT_EQUAL_INT32(ESP_OK, json_writer_end(&s_writer));
    TEST_ASSERT_EQUAL_STRING("{\"a/type\":\"t\",\"a/unit\":\"u\",\"a/simulated\":true}", big);
}

TEST_CASE("test_json_writer_catalog_item_without_unit_omits_it", "[json_writer]")
{
    char big[CATALOG_BUFFER_LEN] = {0};
    json_writer_begin(&s_writer, big, sizeof(big));
    json_writer_add_catalog_item(&s_writer, &(json_catalog_item_t){"a", "t", NULL, false});

    TEST_ASSERT_EQUAL_INT32(ESP_OK, json_writer_end(&s_writer));
    TEST_ASSERT_EQUAL_STRING("{\"a/type\":\"t\",\"a/simulated\":false}", big);
}

TEST_CASE("test_json_writer_catalog_item_too_big_returns_invalid_size", "[json_writer]")
{
    char small[8] = {0};
    json_writer_begin(&s_writer, small, sizeof(small));
    json_writer_add_catalog_item(&s_writer, &(json_catalog_item_t){"a", "t", "u", true});

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_SIZE, json_writer_end(&s_writer));
}
