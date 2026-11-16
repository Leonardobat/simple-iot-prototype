#include <stdint.h>
#include <string.h>

#include "data_repository.h"
#include "fake_http_client.h"
#include "firebase_rtdb.h"
#include "unity.h"

/* Real data_repository + firebase_rtdb + firebase_auth (auth disabled) over the fake HAL. */

#define DB_URL       "https://test-db.example.com"
#define READINGS_URL DB_URL "/readings/test-device.json"
#define FIRST_MS     1730660000000LL
#define SECOND_MS    1730660005000LL
#define SENSORS      "{\"temperature_1\":23.5}"
#define ACTUATORS    "{\"light_1\":true}"

void setUp(void)
{
    fake_http_client_reset();
    TEST_ASSERT_EQUAL_INT32(ESP_OK, firebase_rtdb_init());
}

void tearDown(void)
{
    firebase_rtdb_deinit();
}

static data_repository_reading_t reading_at(int64_t timestamp_ms)
{
    const data_repository_reading_t reading = {
        .timestamp_ms = timestamp_ms, .sensors_json = SENSORS, .actuators_json = ACTUATORS};
    return reading;
}

TEST_CASE("test_data_repository_push_readings_patches_the_device_readings_node",
          "[data_repository]")
{
    const data_repository_reading_t reading = reading_at(FIRST_MS);

    TEST_ASSERT_EQUAL_INT32(ESP_OK, data_repository_push_readings(&reading, 1U));

    TEST_ASSERT_EQUAL_UINT32(1U, fake_http_client.patch_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, fake_http_client.put_calls);
    TEST_ASSERT_EQUAL_STRING(READINGS_URL, fake_http_client.last_url);
}

TEST_CASE("test_data_repository_push_readings_keys_each_reading_by_its_timestamp",
          "[data_repository]")
{
    const data_repository_reading_t readings[] = {reading_at(FIRST_MS), reading_at(SECOND_MS)};

    TEST_ASSERT_EQUAL_INT32(ESP_OK, data_repository_push_readings(readings, 2U));

    TEST_ASSERT_EQUAL_STRING("{\"1730660000000\":{\"sensors\":" SENSORS ",\"actuators\":" ACTUATORS
                             "},\"1730660005000\":{\"sensors\":" SENSORS ",\"actuators\":" ACTUATORS
                             "}}",
                             fake_http_client.last_body);
}

TEST_CASE("test_data_repository_push_readings_zero_count_returns_invalid_arg", "[data_repository]")
{
    const data_repository_reading_t reading = reading_at(FIRST_MS);

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_ARG, data_repository_push_readings(&reading, 0U));
    TEST_ASSERT_EQUAL_UINT32(0U, fake_http_client.patch_calls);
}

TEST_CASE("test_data_repository_push_readings_more_than_the_batch_limit_returns_invalid_size",
          "[data_repository]")
{
    data_repository_reading_t readings[DATA_REPOSITORY_BATCH_MAX + 1U];
    for (size_t i = 0U; i < (sizeof(readings) / sizeof(readings[0])); i++) {
        readings[i] = reading_at(FIRST_MS + (int64_t)i);
    }

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_SIZE, data_repository_push_readings(
                                                      readings, DATA_REPOSITORY_BATCH_MAX + 1U));
    TEST_ASSERT_EQUAL_UINT32(0U, fake_http_client.patch_calls);
}

TEST_CASE("test_data_repository_push_readings_without_auth_sends_no_token", "[data_repository]")
{
    const data_repository_reading_t reading = reading_at(FIRST_MS);

    TEST_ASSERT_EQUAL_INT32(ESP_OK, data_repository_push_readings(&reading, 1U));

    TEST_ASSERT_NULL(strchr(fake_http_client.last_url, '?'));
    TEST_ASSERT_EQUAL_UINT32(0U, fake_http_client.post_calls);
}

TEST_CASE("test_data_repository_push_readings_database_error_propagates", "[data_repository]")
{
    const data_repository_reading_t reading = reading_at(FIRST_MS);
    fake_http_client.patch_result = ESP_ERR_TIMEOUT;

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_TIMEOUT, data_repository_push_readings(&reading, 1U));
}

TEST_CASE("test_data_repository_publish_catalog_patches_both_collections", "[data_repository]")
{
    TEST_ASSERT_EQUAL_INT32(
        ESP_OK, data_repository_publish_catalog("{\"s/type\":\"t\"}", "{\"a/type\":\"light\"}"));

    TEST_ASSERT_EQUAL_UINT32(2U, fake_http_client.patch_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, fake_http_client.put_calls);
    TEST_ASSERT_EQUAL_STRING(DB_URL "/devices/test-device/actuators.json",
                             fake_http_client.last_url);
    TEST_ASSERT_EQUAL_STRING("{\"a/type\":\"light\"}", fake_http_client.last_body);
}

TEST_CASE("test_data_repository_publish_catalog_error_stops_after_sensors", "[data_repository]")
{
    fake_http_client.patch_result = ESP_FAIL;

    TEST_ASSERT_EQUAL_INT32(ESP_FAIL, data_repository_publish_catalog("{}", "{}"));
    TEST_ASSERT_EQUAL_UINT32(1U, fake_http_client.patch_calls);
    TEST_ASSERT_EQUAL_STRING(DB_URL "/devices/test-device/sensors.json", fake_http_client.last_url);
}
