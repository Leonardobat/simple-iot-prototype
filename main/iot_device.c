#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "actuator_controller.h"
#include "config_store.h"
#include "connectivity_provider.h"
#include "device_listener.h"
#include "esp_err.h"
#include "esp_log.h"
#include "firebase_rtdb.h"
#include "flash_fs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iot_device_config.h"
#include "reading_store.h"
#include "reading_uploader.h"
#include "sensor_provider.h"
#include "utils.h"

static const char *TAG = "main";

/* The enabled flags are kept as bit masks: bit i is sensor i or actuator i. */
static_assert((uint32_t)SENSOR_COUNT <= 32U, "A sensor mask holds 32 sensors");
static_assert((uint32_t)ACTUATOR_COUNT <= 32U, "An actuator mask holds 32 actuators");

/* ---- Enabled flags (kept in flash) ---- */

static config_store_enabled_t current_enabled(void)
{
    config_store_enabled_t enabled = {0};
    for (uint32_t i = 0U; i < (uint32_t)SENSOR_COUNT; i++) {
        if (sensor_provider_is_enabled((sensor_t)i)) {
            enabled.sensors |= (uint32_t)(1U << i);
        }
    }
    for (uint32_t i = 0U; i < (uint32_t)ACTUATOR_COUNT; i++) {
        if (actuator_controller_is_enabled((actuator_t)i)) {
            enabled.actuators |= (uint32_t)(1U << i);
        }
    }
    return enabled;
}

static void apply_enabled(const config_store_enabled_t *enabled)
{
    for (uint32_t i = 0U; i < (uint32_t)SENSOR_COUNT; i++) {
        sensor_provider_set_enabled((sensor_t)i, ((enabled->sensors >> i) & 1U) != 0U);
    }
    for (uint32_t i = 0U; i < (uint32_t)ACTUATOR_COUNT; i++) {
        actuator_controller_set_enabled((actuator_t)i, ((enabled->actuators >> i) & 1U) != 0U);
    }
}

/* Returns what flash holds after the restore: the SPA overrides it with the first stream event. */
static config_store_enabled_t restore_enabled(void)
{
    config_store_enabled_t saved = {0};
    const esp_err_t err = config_store_load(&saved);
    if (err == ESP_OK) {
        apply_enabled(&saved);
        ESP_LOGI(TAG, "Enabled flags restored from flash");
    } else if (err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Saved config ignored (%s)", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "No saved config: everything starts disabled");
    }
    return saved;
}

/* Writes the flags only when one changed since the last write. */
static void save_enabled_if_changed(config_store_enabled_t *saved)
{
    const config_store_enabled_t now = current_enabled();
    if ((now.sensors == saved->sensors) && (now.actuators == saved->actuators)) {
        return;
    }
    const esp_err_t err = config_store_save(&now);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Enabled flags not saved (%s), retrying next cycle", esp_err_to_name(err));
        return;
    }
    *saved = now;
}

/* ---- Sampling ---- */

static bool has_value(const sensor_readings_t *readings)
{
    for (uint32_t i = 0U; i < (uint32_t)SENSOR_COUNT; i++) {
        if (isfinite(readings->value[i])) {
            return true;
        }
    }
    return false;
}

/* Stores one reading; the uploader sends it whenever the network allows. */
static void sample_reading(void)
{
    if (!connectivity_provider_is_time_valid()) {
        ESP_LOGD(TAG, "No clock yet: not sampling");
        return;
    }
    reading_record_t record = {0};
    sensor_provider_read(&record.sensors);
    if (!has_value(&record.sensors)) {
        ESP_LOGD(TAG, "No sensor with a value: nothing stored");
        return;
    }
    record.timestamp_ms = utils_epoch_ms();
    actuator_controller_snapshot(&record.actuators);
    const esp_err_t err = reading_store_append(&record);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Reading not stored: %s", esp_err_to_name(err));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(flash_fs_mount());
    ESP_ERROR_CHECK(reading_store_init());
    ESP_ERROR_CHECK(connectivity_provider_init());
    ESP_ERROR_CHECK(firebase_rtdb_init());
    ESP_ERROR_CHECK(sensor_provider_init());
    ESP_ERROR_CHECK(actuator_controller_init());
    config_store_enabled_t saved = restore_enabled();
    ESP_ERROR_CHECK(device_listener_init());
    ESP_ERROR_CHECK(reading_uploader_init(connectivity_provider_is_connected));

    TickType_t last_sample = xTaskGetTickCount();
    for (;;) {
        sample_reading();
        save_enabled_if_changed(&saved);
        if (xTaskDelayUntil(&last_sample, pdMS_TO_TICKS(IOT_DEVICE_SAMPLE_INTERVAL_MS)) ==
            pdFALSE) {
            /* The cycle overran the interval: sample again at once, but restart the cadence
             * instead of catching up with one back-to-back sample per missed interval. */
            last_sample = xTaskGetTickCount();
        }
    }
}
