#include "reading_store.h"

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include "esp_check.h"
#include "flash_fs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "reading_store_config.h"
#include "utils.h"

static const char *TAG = "reading_store";

/* Segment header: magic (4 B), format version (2 B), sensor count (1 B), actuator count (1 B). */
#define HEADER_SIZE 8U
/* Record: timestamp, sensor values, actuator words, then the CRC32 of everything before it. */
#define RECORD_BODY_SIZE (sizeof(int64_t) + sizeof(sensor_readings_t) + sizeof(actuator_snapshot_t))
#define RECORD_SIZE      (RECORD_BODY_SIZE + sizeof(uint32_t))
#define SEGMENT_BYTES    (HEADER_SIZE + (READING_STORE_SEGMENT_RECORDS * RECORD_SIZE))

/* The encoding copies the arrays member by member: no padding may hide in these types. */
static_assert(sizeof(sensor_readings_t) == (sizeof(float) * (size_t)SENSOR_COUNT),
              "sensor_readings_t has padding");
static_assert(sizeof(actuator_snapshot_t) == (sizeof(uint32_t) * (size_t)ACTUATOR_COUNT),
              "actuator_snapshot_t has padding");

static_assert((uint32_t)SENSOR_COUNT <= UINT8_MAX,
              "The segment header holds the sensor count in 1 B");
static_assert((uint32_t)ACTUATOR_COUNT <= UINT8_MAX,
              "The segment header holds the actuator count in 1 B");

static SemaphoreHandle_t s_lock;
/* Segment numbers are contiguous from s_read_seq (the oldest, being sent) to s_last_seq (the one
 * being appended); 0 means no segment. s_read_index is the next unsent record of s_read_seq. */
static uint32_t s_read_seq;
static uint32_t s_last_seq;
static size_t s_read_index;
/* The active segment takes no more records: it is full, or an append to it failed. */
static bool s_active_closed;
/* The newest record appended since init, for reading_store_latest(). */
static reading_record_t s_latest;
static bool s_has_latest;

/* ---- Encoding ---- */

static void put_bytes(uint8_t *buffer, size_t *offset, const void *source, size_t size)
{
    memcpy(&buffer[*offset], source, size);
    *offset += size;
}

static void get_bytes(const uint8_t *buffer, size_t *offset, void *destination, size_t size)
{
    memcpy(destination, &buffer[*offset], size);
    *offset += size;
}

static void encode_record(const reading_record_t *record, uint8_t *buffer)
{
    size_t offset = 0U;
    put_bytes(buffer, &offset, &record->timestamp_ms, sizeof(record->timestamp_ms));
    put_bytes(buffer, &offset, record->sensors.value, sizeof(record->sensors.value));
    put_bytes(buffer, &offset, record->actuators.state, sizeof(record->actuators.state));
    const uint32_t crc = utils_crc32(buffer, offset);
    put_bytes(buffer, &offset, &crc, sizeof(crc));
}

/* false when the CRC does not match; @p record is then not valid. */
static bool decode_record(const uint8_t *buffer, reading_record_t *record)
{
    uint32_t stored_crc = 0U;
    size_t offset = RECORD_BODY_SIZE;
    get_bytes(buffer, &offset, &stored_crc, sizeof(stored_crc));
    if (utils_crc32(buffer, RECORD_BODY_SIZE) != stored_crc) {
        return false;
    }
    offset = 0U;
    get_bytes(buffer, &offset, &record->timestamp_ms, sizeof(record->timestamp_ms));
    get_bytes(buffer, &offset, record->sensors.value, sizeof(record->sensors.value));
    get_bytes(buffer, &offset, record->actuators.state, sizeof(record->actuators.state));
    return true;
}

static void encode_header(uint8_t *buffer)
{
    const uint32_t magic = READING_STORE_MAGIC;
    const uint16_t version = (uint16_t)READING_STORE_FORMAT_VERSION;
    const uint8_t sensors = (uint8_t)SENSOR_COUNT;
    const uint8_t actuators = (uint8_t)ACTUATOR_COUNT;
    size_t offset = 0U;
    put_bytes(buffer, &offset, &magic, sizeof(magic));
    put_bytes(buffer, &offset, &version, sizeof(version));
    put_bytes(buffer, &offset, &sensors, sizeof(sensors));
    put_bytes(buffer, &offset, &actuators, sizeof(actuators));
}

static bool header_is_current(const uint8_t *buffer)
{
    uint8_t expected[HEADER_SIZE] = {0};
    encode_header(expected);
    return memcmp(buffer, expected, HEADER_SIZE) == 0;
}

/* ---- Segment files ---- */

static esp_err_t segment_name(char *name, uint32_t seq)
{
    return utils_snprintf(name, FLASH_FS_NAME_MAX_LEN, READING_STORE_NAME_FORMAT, seq);
}

/* Parses "seg_<8 digits>.bin"; false for any other name. */
static bool parse_segment_name(const char *name, uint32_t *seq)
{
    const size_t prefix_len = strlen(READING_STORE_NAME_PREFIX);
    const size_t suffix_len = strlen(READING_STORE_NAME_SUFFIX);
    if ((strlen(name) != (prefix_len + READING_STORE_NAME_DIGITS + suffix_len)) ||
        (strncmp(name, READING_STORE_NAME_PREFIX, prefix_len) != 0) ||
        (strcmp(&name[prefix_len + READING_STORE_NAME_DIGITS], READING_STORE_NAME_SUFFIX) != 0)) {
        return false;
    }
    uint32_t value = 0U;
    for (size_t i = 0U; i < READING_STORE_NAME_DIGITS; i++) {
        const char digit = name[prefix_len + i];
        if ((digit < '0') || (digit > '9')) {
            return false;
        }
        value = (value * READING_STORE_DECIMAL_BASE) + (uint32_t)(digit - '0');
    }
    *seq = value;
    return value != 0U; /* 0 means "no segment". */
}

/* Whole records in segment @p seq (a cut record at the end does not count). */
static esp_err_t segment_records(uint32_t seq, size_t *count)
{
    char name[FLASH_FS_NAME_MAX_LEN] = {0};
    size_t size = 0U;
    ESP_RETURN_ON_ERROR(segment_name(name, seq), TAG, "Segment name");
    ESP_RETURN_ON_ERROR(flash_fs_size(name, &size), TAG, "Segment size");
    *count = (size > HEADER_SIZE) ? ((size - HEADER_SIZE) / RECORD_SIZE) : 0U;
    return ESP_OK;
}

static void remove_segment(uint32_t seq)
{
    char name[FLASH_FS_NAME_MAX_LEN] = {0};
    if ((segment_name(name, seq) != ESP_OK) || (flash_fs_remove(name) != ESP_OK)) {
        ESP_LOGW(TAG, "Segment %" PRIu32 " not removed", seq);
    }
}

static bool segment_header_is_current(uint32_t seq)
{
    char name[FLASH_FS_NAME_MAX_LEN] = {0};
    uint8_t header[HEADER_SIZE] = {0};
    flash_fs_buffer_t buffer = {.data = header, .capacity = sizeof(header), .length = 0U};
    return (segment_name(name, seq) == ESP_OK) && (flash_fs_read(name, 0U, &buffer) == ESP_OK) &&
           (buffer.length == HEADER_SIZE) && header_is_current(header);
}

/* ---- Init ---- */

/* Segment numbers found in flash, ascending. */
typedef struct {
    uint32_t seqs[READING_STORE_MAX_SEGMENTS];
    size_t count;
} scan_t;

static void on_file(const char *name, void *ctx)
{
    scan_t *scan = (scan_t *)ctx;
    uint32_t seq = 0U;
    if (!parse_segment_name(name, &seq)) {
        return;
    }
    if (scan->count >= READING_STORE_MAX_SEGMENTS) {
        ESP_LOGW(TAG, "Too many segments, %s ignored", name);
        return;
    }
    size_t position = scan->count;
    while ((position > 0U) && (scan->seqs[position - 1U] > seq)) {
        scan->seqs[position] = scan->seqs[position - 1U];
        position--;
    }
    scan->seqs[position] = seq;
    scan->count++;
}

/* Index of the oldest segment of the newest contiguous run whose header is current. */
static size_t first_usable(const scan_t *scan)
{
    size_t first = scan->count;
    while (first > 0U) {
        const uint32_t seq = scan->seqs[first - 1U];
        const bool contiguous = (first == scan->count) || ((seq + 1U) == scan->seqs[first]);
        if (!contiguous || !segment_header_is_current(seq)) {
            break;
        }
        first--;
    }
    return first;
}

static esp_err_t scan_segments(void)
{
    scan_t scan = {{0}, 0U};
    ESP_RETURN_ON_ERROR(flash_fs_list(on_file, &scan), TAG, "Listing segments");

    const size_t first = first_usable(&scan);
    for (size_t i = 0U; i < first; i++) {
        ESP_LOGW(TAG, "Segment %" PRIu32 " dropped (other format or gap)", scan.seqs[i]);
        remove_segment(scan.seqs[i]);
    }
    s_read_seq = (first < scan.count) ? scan.seqs[first] : 0U;
    s_last_seq = (first < scan.count) ? scan.seqs[scan.count - 1U] : 0U;
    return ESP_OK;
}

esp_err_t reading_store_init(void)
{
    assert(s_lock == NULL);

    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, TAG, "Mutex alloc failed");
    s_read_index = 0U;
    s_has_latest = false;
    s_active_closed = true; /* Whatever the last run left is never appended to. */
    const esp_err_t err = scan_segments();
    if (err != ESP_OK) {
        reading_store_deinit();
    }
    return err;
}

void reading_store_deinit(void)
{
    if (s_lock != NULL) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
    }
    s_read_seq = 0U;
    s_last_seq = 0U;
    s_read_index = 0U;
    s_has_latest = false;
}

/* ---- Append ---- */

/* Drops the oldest segment (sent or not); the one being appended is never dropped. */
static void drop_oldest_segment(void)
{
    ESP_LOGW(TAG, "Low on space: dropping segment %" PRIu32, s_read_seq);
    remove_segment(s_read_seq);
    s_read_seq++;
    s_read_index = 0U;
}

static void make_room(void)
{
    const size_t reserved = (size_t)READING_STORE_RESERVED_SEGMENTS * SEGMENT_BYTES;
    size_t free_bytes = 0U;
    while ((s_read_seq < s_last_seq) && (flash_fs_free_bytes(&free_bytes) == ESP_OK) &&
           (free_bytes < reserved)) {
        drop_oldest_segment();
    }
}

static esp_err_t open_new_segment(void)
{
    char name[FLASH_FS_NAME_MAX_LEN] = {0};
    uint8_t header[HEADER_SIZE] = {0};
    const uint32_t seq = s_last_seq + 1U;
    make_room();
    ESP_RETURN_ON_ERROR(segment_name(name, seq), TAG, "Segment name");
    encode_header(header);
    ESP_RETURN_ON_ERROR(flash_fs_append(name, header, sizeof(header)), TAG, "Segment header");
    if (s_last_seq == 0U) {
        s_read_seq = seq;
        s_read_index = 0U;
    }
    s_last_seq = seq;
    s_active_closed = false;
    return ESP_OK;
}

/* Sets @p full when the active segment cannot take another record. */
static esp_err_t active_is_full(bool *full)
{
    size_t count = 0U;
    *full = s_active_closed || (s_last_seq == 0U);
    if (*full) {
        return ESP_OK;
    }
    /* A lost file counts as full: the next record then opens a new segment. */
    *full =
        (segment_records(s_last_seq, &count) != ESP_OK) || (count >= READING_STORE_SEGMENT_RECORDS);
    return ESP_OK;
}

static esp_err_t append_locked(const reading_record_t *record)
{
    bool full = false;
    ESP_RETURN_ON_ERROR(active_is_full(&full), TAG, "Active segment state");
    if (full) {
        ESP_RETURN_ON_ERROR(open_new_segment(), TAG, "New segment");
    }
    char name[FLASH_FS_NAME_MAX_LEN] = {0};
    uint8_t buffer[RECORD_SIZE] = {0};
    ESP_RETURN_ON_ERROR(segment_name(name, s_last_seq), TAG, "Segment name");
    encode_record(record, buffer);
    const esp_err_t err = flash_fs_append(name, buffer, sizeof(buffer));
    if (err != ESP_OK) {
        s_active_closed = true; /* The file may end in a cut record: never append after it. */
    }
    return err;
}

static esp_err_t take_lock(void)
{
    assert(s_lock != NULL);
    return (xSemaphoreTake(s_lock, pdMS_TO_TICKS(READING_STORE_LOCK_TIMEOUT_MS)) == pdTRUE)
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

esp_err_t reading_store_append(const reading_record_t *record)
{
    assert(record != NULL);
    ESP_RETURN_ON_ERROR(take_lock(), TAG, "Store busy");
    const esp_err_t err = append_locked(record);
    if (err == ESP_OK) {
        s_latest = *record;
        s_has_latest = true;
    }
    (void)xSemaphoreGive(s_lock); /* Cannot fail: taken above by this task. */
    return err;
}

esp_err_t reading_store_latest(reading_record_t *out)
{
    assert(out != NULL);
    ESP_RETURN_ON_ERROR(take_lock(), TAG, "Store busy");
    const bool found = s_has_latest;
    if (found) {
        *out = s_latest;
    }
    (void)xSemaphoreGive(s_lock); /* Cannot fail: taken above by this task. */
    return found ? ESP_OK : ESP_ERR_NOT_FOUND;
}

/* ---- Peek and confirm ---- */

/* True when the segment being sent is closed and every record in it was confirmed (or the file is
 * gone). An I/O error keeps it: better to send twice than to lose it. */
static bool read_segment_is_done(void)
{
    size_t count = 0U;
    if ((s_read_seq == 0U) || (s_read_seq == s_last_seq)) {
        return false;
    }
    const esp_err_t err = segment_records(s_read_seq, &count);
    return (err == ESP_ERR_NOT_FOUND) || ((err == ESP_OK) && (s_read_index >= count));
}

/* Moves past the oldest segments that were fully sent, deleting them. */
static void skip_sent_segments(void)
{
    while (read_segment_is_done()) {
        remove_segment(s_read_seq);
        s_read_seq++;
        s_read_index = 0U;
    }
}

/* Reads the record at @p index of the segment being sent; false if it is corrupt. */
static esp_err_t read_record(size_t index, reading_record_t *record, bool *valid)
{
    char name[FLASH_FS_NAME_MAX_LEN] = {0};
    uint8_t bytes[RECORD_SIZE] = {0};
    flash_fs_buffer_t buffer = {.data = bytes, .capacity = sizeof(bytes), .length = 0U};
    ESP_RETURN_ON_ERROR(segment_name(name, s_read_seq), TAG, "Segment name");
    ESP_RETURN_ON_ERROR(flash_fs_read(name, HEADER_SIZE + (index * RECORD_SIZE), &buffer), TAG,
                        "Record read");
    *valid = (buffer.length == RECORD_SIZE) && decode_record(bytes, record);
    return ESP_OK;
}

static esp_err_t peek_locked(reading_record_t *out, size_t max_records, size_t *out_count)
{
    size_t available = 0U;
    skip_sent_segments();
    *out_count = 0U;
    if (s_read_seq == 0U) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(segment_records(s_read_seq, &available), TAG, "Segment records");
    while ((*out_count < max_records) && ((s_read_index + *out_count) < available)) {
        bool valid = false;
        ESP_RETURN_ON_ERROR(read_record(s_read_index + *out_count, &out[*out_count], &valid), TAG,
                            "Record");
        if (valid) {
            (*out_count)++;
        } else if (*out_count == 0U) {
            ESP_LOGW(TAG, "Corrupt record %zu of segment %" PRIu32 " skipped", s_read_index,
                     s_read_seq);
            s_read_index++;
        } else {
            break; /* Delivers the valid ones first; the corrupt one is skipped next call. */
        }
    }
    return ESP_OK;
}

esp_err_t reading_store_peek(reading_record_t *out, size_t max_records, size_t *out_count)
{
    assert((out != NULL) && (out_count != NULL) && (max_records > 0U));
    ESP_RETURN_ON_ERROR(take_lock(), TAG, "Store busy");
    const esp_err_t err = peek_locked(out, max_records, out_count);
    (void)xSemaphoreGive(s_lock); /* Cannot fail: taken above by this task. */
    return err;
}

esp_err_t reading_store_confirm(size_t count)
{
    ESP_RETURN_ON_ERROR(take_lock(), TAG, "Store busy");
    s_read_index += count;
    (void)xSemaphoreGive(s_lock); /* Cannot fail: taken above by this task. */
    return ESP_OK;
}
