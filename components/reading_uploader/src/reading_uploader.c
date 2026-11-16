#include "reading_uploader.h"

#include <assert.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "actuator_controller.h"
#include "backoff.h"
#include "data_repository.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "reading_store.h"
#include "reading_uploader_config.h"
#include "sensor_provider.h"

static const char *TAG = "reading_uploader";

/* Used only by the upload task (or a test calling reading_uploader_step() while it is offline). */
static reading_record_t s_records[DATA_REPOSITORY_BATCH_MAX];
static char s_sensors_json[DATA_REPOSITORY_BATCH_MAX][SENSOR_READINGS_JSON_MAX_LEN];
static char s_actuators_json[DATA_REPOSITORY_BATCH_MAX][ACTUATOR_STATES_JSON_MAX_LEN];
static data_repository_reading_t s_batch[DATA_REPOSITORY_BATCH_MAX];
static char s_catalog_sensors_json[SENSOR_CATALOG_JSON_MAX_LEN];
static char s_catalog_actuators_json[ACTUATOR_CATALOG_JSON_MAX_LEN];
static bool s_catalog_published;

static reading_uploader_online_fn_t s_is_online;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_task_exited;
static atomic_bool s_stop;

/* ---- One attempt ---- */

static esp_err_t publish_catalog(void)
{
    ESP_RETURN_ON_ERROR(
        sensor_provider_catalog_to_json(s_catalog_sensors_json, sizeof(s_catalog_sensors_json)),
        TAG, "Sensor catalog");
    ESP_RETURN_ON_ERROR(actuator_controller_catalog_to_json(s_catalog_actuators_json,
                                                            sizeof(s_catalog_actuators_json)),
                        TAG, "Actuator catalog");
    ESP_RETURN_ON_ERROR(
        data_repository_publish_catalog(s_catalog_sensors_json, s_catalog_actuators_json), TAG,
        "Catalog not published");
    ESP_LOGI(TAG, "Catalog published");
    return ESP_OK;
}

/* Fills s_batch[index] from s_records[index]. */
static esp_err_t prepare_reading(size_t index)
{
    const reading_record_t *record = &s_records[index];
    ESP_RETURN_ON_ERROR(sensor_readings_to_json(&record->sensors, s_sensors_json[index],
                                                sizeof(s_sensors_json[index])),
                        TAG, "Sensors JSON");
    ESP_RETURN_ON_ERROR(actuator_snapshot_to_json(&record->actuators, s_actuators_json[index],
                                                  sizeof(s_actuators_json[index])),
                        TAG, "Actuators JSON");
    s_batch[index] = (data_repository_reading_t){.timestamp_ms = record->timestamp_ms,
                                                 .sensors_json = s_sensors_json[index],
                                                 .actuators_json = s_actuators_json[index]};
    return ESP_OK;
}

static esp_err_t send_batch(void)
{
    size_t count = 0U;
    ESP_RETURN_ON_ERROR(reading_store_peek(s_records, DATA_REPOSITORY_BATCH_MAX, &count), TAG,
                        "Stored readings not read");
    if (count == 0U) {
        return ESP_OK;
    }
    for (size_t i = 0U; i < count; i++) {
        ESP_RETURN_ON_ERROR(prepare_reading(i), TAG, "Reading %zu", i);
    }
    ESP_RETURN_ON_ERROR(data_repository_push_readings(s_batch, count), TAG, "Batch not sent");
    ESP_LOGI(TAG, "Sent %zu readings up to %" PRId64, count, s_batch[count - 1U].timestamp_ms);
    return reading_store_confirm(count);
}

esp_err_t reading_uploader_step(void)
{
    if (!s_catalog_published) {
        ESP_RETURN_ON_ERROR(publish_catalog(), TAG, "Catalog step");
        s_catalog_published = true;
    }
    return send_batch();
}

/* ---- Task ---- */

/* Waits before the next attempt; reading_uploader_deinit() wakes it up early. */
static void wait_for_next_attempt(uint32_t delay_ms)
{
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delay_ms)); /* Returns early only on deinit. */
}

static void upload_task(void *arg)
{
    (void)arg; /* No argument: the state is static. */
    uint32_t retry_ms = 0U;
    uint32_t delay_ms = 0U; /* First attempt at once. */
    while (!atomic_load(&s_stop)) {
        wait_for_next_attempt(delay_ms);
        delay_ms = READING_UPLOADER_PERIOD_MS;
        if (atomic_load(&s_stop) || !s_is_online()) {
            retry_ms = 0U;
            continue;
        }
        const esp_err_t err = reading_uploader_step();
        if (err == ESP_OK) {
            retry_ms = 0U;
            continue;
        }
        retry_ms = backoff_next_delay_ms(retry_ms, READING_UPLOADER_PERIOD_MS,
                                         READING_UPLOADER_RETRY_MAX_MS);
        delay_ms = retry_ms;
        ESP_LOGW(TAG, "Upload failed (%s), retrying in %" PRIu32 " ms", esp_err_to_name(err),
                 retry_ms);
        ESP_LOGD(TAG, "Stack high water mark: %u bytes",
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
    (void)xSemaphoreGive(s_task_exited); /* Cannot fail: binary semaphore given once. */
    vTaskDelete(NULL);
}

esp_err_t reading_uploader_init(reading_uploader_online_fn_t is_online)
{
    assert((s_task == NULL) && (is_online != NULL));

    s_is_online = is_online;
    atomic_store(&s_stop, false);
    s_task_exited = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_task_exited != NULL, ESP_ERR_NO_MEM, TAG, "Semaphore alloc failed");
    if (xTaskCreate(upload_task, "reading_upload", READING_UPLOADER_TASK_STACK_SIZE, NULL,
                    READING_UPLOADER_TASK_PRIORITY, &s_task) != pdPASS) {
        vSemaphoreDelete(s_task_exited);
        s_task_exited = NULL;
        s_task = NULL;
        ESP_LOGE(TAG, "Task creation failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t reading_uploader_deinit(void)
{
    if (s_task == NULL) {
        return ESP_OK;
    }
    atomic_store(&s_stop, true);
    (void)xTaskNotifyGive(s_task); /* Always pdPASS: wakes the wait. */
    ESP_RETURN_ON_FALSE(
        xSemaphoreTake(s_task_exited, pdMS_TO_TICKS(READING_UPLOADER_STOP_TIMEOUT_MS)) == pdTRUE,
        ESP_ERR_TIMEOUT, TAG, "Upload task did not stop");
    vSemaphoreDelete(s_task_exited);
    s_task_exited = NULL;
    s_task = NULL;
    s_catalog_published = false;
    return ESP_OK;
}
