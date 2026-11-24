#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "data_repository.h"
#include "fake_flash_fs.h"
#include "fake_http_client.h"
#include "firebase_rtdb.h"
#include "flash_fs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "reading_store.h"
#include "reading_uploader.h"
#include "unity.h"

/* Real uploader + store + data_repository + Firebase stack over the fake HALs. */

#define READINGS_URL  "https://test-db.example.com/readings/test-device.json"
#define BASE_MS       1000
#define TASK_WAIT_MS  2000U
#define TASK_POLL_MS  10U
#define CATALOG_CALLS 2U /* One PATCH per collection. */

static bool s_online;

static bool is_online(void)
{
    return s_online;
}

/* Record n: timestamp BASE + n, first sensor n, the others NaN, actuators all off and disabled. */
static void append_record(uint32_t n)
{
    reading_record_t record = {0};
    record.timestamp_ms = BASE_MS + (int64_t)n;
    for (uint32_t i = 0U; i < (uint32_t)SENSOR_COUNT; i++) {
        record.sensors.value[i] = NAN;
    }
    record.sensors.value[0] = (float)n;
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_store_append(&record));
}

static size_t pending_count(void)
{
    reading_record_t records[DATA_REPOSITORY_BATCH_MAX];
    size_t count = 0U;
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_store_peek(records, DATA_REPOSITORY_BATCH_MAX, &count));
    return count;
}

/* The task is started offline, so it never races with the steps the tests run by hand. */
void setUp(void)
{
    s_online = false;
    fake_http_client_reset();
    fake_flash_fs_reset();
    TEST_ASSERT_EQUAL_INT32(ESP_OK, flash_fs_mount());
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_store_init());
    TEST_ASSERT_EQUAL_INT32(ESP_OK, firebase_rtdb_init());
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_uploader_init(is_online));
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_uploader_deinit());
    firebase_rtdb_deinit();
    reading_store_deinit();
    flash_fs_unmount();
    fake_flash_fs_reset();
}

TEST_CASE("test_reading_uploader_first_step_publishes_the_catalog_once", "[reading_uploader]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_uploader_step());
    TEST_ASSERT_EQUAL_UINT32(CATALOG_CALLS, fake_http_client.patch_calls);

    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_uploader_step());
    TEST_ASSERT_EQUAL_UINT32(CATALOG_CALLS, fake_http_client.patch_calls);
}

TEST_CASE("test_reading_uploader_catalog_failure_is_reported_and_retried", "[reading_uploader]")
{
    append_record(1U);
    fake_http_client.patch_result = ESP_FAIL;
    TEST_ASSERT_EQUAL_INT32(ESP_FAIL, reading_uploader_step());
    TEST_ASSERT_EQUAL_UINT32(1U, pending_count());

    fake_http_client.patch_result = ESP_OK;
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_uploader_step());
    TEST_ASSERT_EQUAL_UINT32(0U, pending_count());
}

TEST_CASE("test_reading_uploader_step_sends_the_stored_reading_and_consumes_it",
          "[reading_uploader]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_uploader_step()); /* Catalog. */
    append_record(1U);

    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_uploader_step());

    TEST_ASSERT_EQUAL_STRING(READINGS_URL, fake_http_client.last_url);
    TEST_ASSERT_EQUAL_STRING("{\"1001\":{\"sensors\":{\"temperature_1\":1.0},\"actuators\":{}}}",
                             fake_http_client.last_body);
    TEST_ASSERT_EQUAL_UINT32(0U, pending_count());
}

TEST_CASE("test_reading_uploader_step_sends_at_most_one_batch_oldest_first", "[reading_uploader]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_uploader_step()); /* Catalog. */
    for (uint32_t n = 1U; n <= (DATA_REPOSITORY_BATCH_MAX + 2U); n++) {
        append_record(n);
    }

    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_uploader_step());

    TEST_ASSERT_NOT_NULL(strstr(fake_http_client.last_body, "\"1001\":"));
    TEST_ASSERT_NOT_NULL(strstr(fake_http_client.last_body, "\"1005\":"));
    TEST_ASSERT_NULL(strstr(fake_http_client.last_body, "\"1006\":"));
    TEST_ASSERT_EQUAL_UINT32(2U, pending_count());
}

/* A reset leaves the readings of the last run in flash and forgets which were sent. */
static void reset_store_keeping_the_flash(void)
{
    reading_store_deinit();
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_store_init());
}

TEST_CASE("test_reading_uploader_sends_the_newest_reading_ahead_of_an_older_backlog",
          "[reading_uploader]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_uploader_step()); /* Catalog. */
    for (uint32_t n = 1U; n <= (DATA_REPOSITORY_BATCH_MAX + 2U); n++) {
        append_record(n);
    }
    reset_store_keeping_the_flash();
    append_record(DATA_REPOSITORY_BATCH_MAX + 3U);
    const uint32_t before = fake_http_client.patch_calls;

    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_uploader_step());

    TEST_ASSERT_EQUAL_UINT32(before + 2U, fake_http_client.patch_calls); /* Newest, then batch. */
    TEST_ASSERT_NOT_NULL(strstr(fake_http_client.last_body, "\"1001\":"));
    TEST_ASSERT_NULL(strstr(fake_http_client.last_body, "\"1008\":"));

    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_uploader_step());
    TEST_ASSERT_EQUAL_UINT32(before + 3U, fake_http_client.patch_calls); /* Not sent ahead twice. */
}

TEST_CASE("test_reading_uploader_does_not_send_ahead_when_the_batch_holds_the_newest",
          "[reading_uploader]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_uploader_step()); /* Catalog. */
    append_record(1U);
    append_record(2U);
    const uint32_t before = fake_http_client.patch_calls;

    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_uploader_step());

    TEST_ASSERT_EQUAL_UINT32(before + 1U, fake_http_client.patch_calls);
}

TEST_CASE("test_reading_uploader_failed_send_keeps_the_readings_and_resends_them",
          "[reading_uploader]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_uploader_step()); /* Catalog. */
    append_record(1U);
    fake_http_client.patch_result = ESP_ERR_TIMEOUT;

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_TIMEOUT, reading_uploader_step());
    TEST_ASSERT_EQUAL_UINT32(1U, pending_count());

    fake_http_client.patch_result = ESP_OK;
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_uploader_step());
    TEST_ASSERT_NOT_NULL(strstr(fake_http_client.last_body, "\"1001\":"));
    TEST_ASSERT_EQUAL_UINT32(0U, pending_count());
}

TEST_CASE("test_reading_uploader_nothing_stored_sends_no_reading_request", "[reading_uploader]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_uploader_step());

    TEST_ASSERT_EQUAL_UINT32(CATALOG_CALLS, fake_http_client.patch_calls);
}

TEST_CASE("test_reading_uploader_task_waits_while_offline", "[reading_uploader]")
{
    append_record(1U);

    vTaskDelay(pdMS_TO_TICKS(100U));

    TEST_ASSERT_EQUAL_UINT32(0U, fake_http_client.patch_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, pending_count());
}

TEST_CASE("test_reading_uploader_task_sends_the_catalog_and_readings_once_online",
          "[reading_uploader]")
{
    /* A fresh task, started online: its first attempt is immediate. */
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_uploader_deinit());
    append_record(1U);
    s_online = true;
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_uploader_init(is_online));

    for (uint32_t waited = 0U; (waited < TASK_WAIT_MS) && (pending_count() > 0U);
         waited += TASK_POLL_MS) {
        vTaskDelay(pdMS_TO_TICKS(TASK_POLL_MS));
    }

    TEST_ASSERT_EQUAL_UINT32(0U, pending_count());
    TEST_ASSERT_EQUAL_UINT32(CATALOG_CALLS + 1U, fake_http_client.patch_calls);
}

TEST_CASE("test_reading_uploader_deinit_twice_is_safe", "[reading_uploader]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_uploader_deinit());
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_uploader_deinit());
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_uploader_init(is_online));
}
