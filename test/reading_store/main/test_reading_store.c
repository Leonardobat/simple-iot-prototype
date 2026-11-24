#include <math.h>
#include <stdint.h>
#include <string.h>

#include "fake_flash_fs.h"
#include "flash_fs.h"
#include "reading_store.h"
#include "unity.h"

/* Real reading_store over the fake flash_fs. The records are 56 B, so a segment is ~14 KB. */

#define BASE_TIMESTAMP_MS 1000
#define SEGMENT_PREFIX    "seg_"
#define PEEK_MAX          5U
#define SEGMENT_BYTES     14344U /* 8 B header + 256 records of 56 B. */
#define FIRST_SEGMENT     "seg_00000001.bin"
#define SECOND_SEGMENT    "seg_00000002.bin"
#define RECORD_BYTES      56U

static reading_record_t s_peeked[PEEK_MAX];

/* Record number n: timestamp BASE + n, first sensor n, the others NaN, first actuator n. */
static reading_record_t make_record(uint32_t n)
{
    reading_record_t record = {0};
    record.timestamp_ms = BASE_TIMESTAMP_MS + (int64_t)n;
    for (uint32_t i = 0U; i < (uint32_t)SENSOR_COUNT; i++) {
        record.sensors.value[i] = NAN;
    }
    record.sensors.value[0] = (float)n;
    record.actuators.state[0] = n;
    return record;
}

static void append_records(uint32_t first, uint32_t count)
{
    for (uint32_t n = first; n < (first + count); n++) {
        const reading_record_t record = make_record(n);
        TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_store_append(&record));
    }
}

static size_t peek(size_t max_records)
{
    size_t count = 0U;
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_store_peek(s_peeked, max_records, &count));
    return count;
}

static void assert_peeked_record(size_t index, uint32_t n)
{
    TEST_ASSERT_EQUAL_INT64(BASE_TIMESTAMP_MS + (int64_t)n, s_peeked[index].timestamp_ms);
    TEST_ASSERT_EQUAL_FLOAT((float)n, s_peeked[index].sensors.value[0]);
    TEST_ASSERT_TRUE(isnan(s_peeked[index].sensors.value[1]));
    TEST_ASSERT_EQUAL_UINT32(n, s_peeked[index].actuators.state[0]);
}

static uint32_t segment_file_count(void)
{
    uint32_t count = 0U;
    for (uint32_t i = 0U; i < FAKE_FLASH_FS_MAX_FILES; i++) {
        const fake_flash_file_t *file = &fake_flash_fs.files[i];
        if (file->used && (strncmp(file->name, SEGMENT_PREFIX, strlen(SEGMENT_PREFIX)) == 0)) {
            count++;
        }
    }
    return count;
}

/* Simulates a reset: the flash stays, the store starts again. */
static void restart_store(void)
{
    reading_store_deinit();
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_store_init());
}

void setUp(void)
{
    fake_flash_fs_reset();
    TEST_ASSERT_EQUAL_INT32(ESP_OK, flash_fs_mount());
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_store_init());
}

void tearDown(void)
{
    reading_store_deinit();
    flash_fs_unmount();
    fake_flash_fs_reset();
}

TEST_CASE("test_reading_store_peek_empty_store_returns_nothing", "[reading_store]")
{
    TEST_ASSERT_EQUAL_UINT32(0U, peek(PEEK_MAX));
}

TEST_CASE("test_reading_store_peek_returns_appended_records_oldest_first", "[reading_store]")
{
    append_records(1U, 3U);

    TEST_ASSERT_EQUAL_UINT32(3U, peek(PEEK_MAX));
    assert_peeked_record(0U, 1U);
    assert_peeked_record(1U, 2U);
    assert_peeked_record(2U, 3U);
}

TEST_CASE("test_reading_store_peek_without_confirm_returns_the_same_records", "[reading_store]")
{
    append_records(1U, 2U);
    TEST_ASSERT_EQUAL_UINT32(2U, peek(PEEK_MAX));

    TEST_ASSERT_EQUAL_UINT32(2U, peek(PEEK_MAX));
    assert_peeked_record(0U, 1U);
}

TEST_CASE("test_reading_store_confirm_consumes_only_the_confirmed_records", "[reading_store]")
{
    append_records(1U, 3U);
    TEST_ASSERT_EQUAL_UINT32(3U, peek(PEEK_MAX));
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_store_confirm(2U));

    TEST_ASSERT_EQUAL_UINT32(1U, peek(PEEK_MAX));
    assert_peeked_record(0U, 3U);
}

TEST_CASE("test_reading_store_peek_limits_the_records_to_max", "[reading_store]")
{
    append_records(1U, 8U);

    TEST_ASSERT_EQUAL_UINT32(PEEK_MAX, peek(PEEK_MAX));
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_store_confirm(PEEK_MAX));
    TEST_ASSERT_EQUAL_UINT32(3U, peek(PEEK_MAX));
    assert_peeked_record(0U, 6U);
}

TEST_CASE("test_reading_store_append_full_segment_opens_a_new_one", "[reading_store]")
{
    append_records(1U, READING_STORE_SEGMENT_RECORDS);
    TEST_ASSERT_EQUAL_UINT32(1U, segment_file_count());

    append_records(READING_STORE_SEGMENT_RECORDS + 1U, 1U);
    TEST_ASSERT_EQUAL_UINT32(2U, segment_file_count());
}

TEST_CASE("test_reading_store_closed_segment_is_deleted_only_after_all_records_are_confirmed",
          "[reading_store]")
{
    append_records(1U, READING_STORE_SEGMENT_RECORDS + 1U);
    TEST_ASSERT_EQUAL_UINT32(PEEK_MAX, peek(PEEK_MAX));
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_store_confirm(PEEK_MAX));
    TEST_ASSERT_EQUAL_UINT32(0U, fake_flash_fs.remove_calls);

    for (uint32_t sent = PEEK_MAX; sent < READING_STORE_SEGMENT_RECORDS;) {
        const size_t count = peek(PEEK_MAX);
        TEST_ASSERT_GREATER_THAN_UINT32(0U, (uint32_t)count);
        TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_store_confirm(count));
        sent += (uint32_t)count;
    }
    TEST_ASSERT_EQUAL_UINT32(0U, fake_flash_fs.remove_calls);

    TEST_ASSERT_EQUAL_UINT32(1U, peek(PEEK_MAX));
    assert_peeked_record(0U, READING_STORE_SEGMENT_RECORDS + 1U);
    TEST_ASSERT_EQUAL_UINT32(1U, fake_flash_fs.remove_calls);
    TEST_ASSERT_EQUAL_UINT32(1U, segment_file_count());
}

TEST_CASE("test_reading_store_active_segment_is_kept_when_fully_confirmed", "[reading_store]")
{
    append_records(1U, 2U);
    TEST_ASSERT_EQUAL_UINT32(2U, peek(PEEK_MAX));
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_store_confirm(2U));

    TEST_ASSERT_EQUAL_UINT32(0U, peek(PEEK_MAX));
    append_records(3U, 1U);
    TEST_ASSERT_EQUAL_UINT32(1U, peek(PEEK_MAX));
    assert_peeked_record(0U, 3U);
    TEST_ASSERT_EQUAL_UINT32(0U, fake_flash_fs.remove_calls);
}

TEST_CASE("test_reading_store_init_keeps_the_records_of_the_previous_run", "[reading_store]")
{
    append_records(1U, 3U);

    restart_store();

    TEST_ASSERT_EQUAL_UINT32(3U, peek(PEEK_MAX));
    assert_peeked_record(2U, 3U);
}

TEST_CASE("test_reading_store_latest_is_the_newest_record_since_init", "[reading_store]")
{
    reading_record_t latest = {0};
    TEST_ASSERT_EQUAL_INT32(ESP_ERR_NOT_FOUND, reading_store_latest(&latest));

    append_records(1U, 3U);
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_store_latest(&latest));
    TEST_ASSERT_EQUAL_INT64(BASE_TIMESTAMP_MS + 3, latest.timestamp_ms);
    TEST_ASSERT_EQUAL_UINT32(3U, peek(PEEK_MAX)); /* Nothing was consumed. */

    restart_store(); /* The previous run's records are not "latest" any more. */
    TEST_ASSERT_EQUAL_INT32(ESP_ERR_NOT_FOUND, reading_store_latest(&latest));
}

TEST_CASE("test_reading_store_append_after_restart_opens_a_new_segment", "[reading_store]")
{
    append_records(1U, 2U);
    restart_store();

    append_records(3U, 1U);

    TEST_ASSERT_EQUAL_UINT32(2U, segment_file_count());
    TEST_ASSERT_EQUAL_UINT32(2U, peek(PEEK_MAX));
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_store_confirm(2U));
    TEST_ASSERT_EQUAL_UINT32(1U, peek(PEEK_MAX));
    assert_peeked_record(0U, 3U);
}

TEST_CASE("test_reading_store_init_ignores_a_record_cut_by_a_power_loss", "[reading_store]")
{
    append_records(1U, 3U);
    size_t size = 0U;
    TEST_ASSERT_EQUAL_INT32(ESP_OK, flash_fs_size(FIRST_SEGMENT, &size));
    TEST_ASSERT_TRUE(fake_flash_fs_truncate(FIRST_SEGMENT, size - 10U));

    restart_store();

    TEST_ASSERT_EQUAL_UINT32(2U, peek(PEEK_MAX));
    assert_peeked_record(1U, 2U);
}

TEST_CASE("test_reading_store_peek_skips_a_corrupt_record_in_the_middle", "[reading_store]")
{
    append_records(1U, 3U);
    const size_t second_record = 8U + RECORD_BYTES;
    TEST_ASSERT_TRUE(fake_flash_fs_corrupt(FIRST_SEGMENT, second_record + 3U));

    TEST_ASSERT_EQUAL_UINT32(1U, peek(PEEK_MAX));
    assert_peeked_record(0U, 1U);
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_store_confirm(1U));

    TEST_ASSERT_EQUAL_UINT32(1U, peek(PEEK_MAX));
    assert_peeked_record(0U, 3U);
}

TEST_CASE("test_reading_store_peek_skips_a_corrupt_first_record", "[reading_store]")
{
    append_records(1U, 2U);
    TEST_ASSERT_TRUE(fake_flash_fs_corrupt(FIRST_SEGMENT, 8U + 3U));

    TEST_ASSERT_EQUAL_UINT32(1U, peek(PEEK_MAX));
    assert_peeked_record(0U, 2U);
}

TEST_CASE("test_reading_store_init_drops_a_segment_of_another_format_and_the_older_ones",
          "[reading_store]")
{
    append_records(1U, 1U);
    restart_store();
    append_records(2U, 1U);
    restart_store();
    append_records(3U, 1U);
    TEST_ASSERT_EQUAL_UINT32(3U, segment_file_count());
    TEST_ASSERT_TRUE(fake_flash_fs_corrupt(SECOND_SEGMENT, 0U));

    restart_store();

    TEST_ASSERT_EQUAL_UINT32(1U, segment_file_count());
    TEST_ASSERT_EQUAL_UINT32(1U, peek(PEEK_MAX));
    assert_peeked_record(0U, 3U);
}

TEST_CASE("test_reading_store_init_ignores_files_that_are_not_segments", "[reading_store]")
{
    const uint8_t data[] = {1U, 2U, 3U};
    TEST_ASSERT_EQUAL_INT32(ESP_OK, flash_fs_append("config.bin", data, sizeof(data)));
    TEST_ASSERT_EQUAL_INT32(ESP_OK, flash_fs_append("seg_abc.bin", data, sizeof(data)));

    restart_store();

    TEST_ASSERT_EQUAL_UINT32(0U, peek(PEEK_MAX));
    TEST_ASSERT_EQUAL_UINT32(0U, fake_flash_fs.remove_calls);
}

TEST_CASE("test_reading_store_append_failure_closes_the_segment", "[reading_store]")
{
    append_records(1U, 1U);
    const reading_record_t record = make_record(2U);
    fake_flash_fs.append_result = ESP_FAIL;
    TEST_ASSERT_EQUAL_INT32(ESP_FAIL, reading_store_append(&record));
    fake_flash_fs.append_result = ESP_OK;

    append_records(3U, 1U);

    TEST_ASSERT_EQUAL_UINT32(2U, segment_file_count());
    TEST_ASSERT_EQUAL_UINT32(1U, peek(PEEK_MAX));
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_store_confirm(1U));
    TEST_ASSERT_EQUAL_UINT32(1U, peek(PEEK_MAX));
    assert_peeked_record(0U, 3U);
}

TEST_CASE("test_reading_store_append_low_on_space_drops_the_oldest_segment", "[reading_store]")
{
    fake_flash_fs.capacity_bytes = 5U * SEGMENT_BYTES;

    append_records(1U, (3U * READING_STORE_SEGMENT_RECORDS) + 1U);

    TEST_ASSERT_EQUAL_UINT32(1U, fake_flash_fs.remove_calls);
    TEST_ASSERT_EQUAL_UINT32(PEEK_MAX, peek(PEEK_MAX));
    assert_peeked_record(0U, READING_STORE_SEGMENT_RECORDS + 1U);
}

TEST_CASE("test_reading_store_deinit_twice_is_safe_and_init_works_again", "[reading_store]")
{
    reading_store_deinit();
    reading_store_deinit(); /* Safe twice. */
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_store_init());
}
