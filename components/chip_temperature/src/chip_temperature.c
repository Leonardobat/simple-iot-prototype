#include "chip_temperature.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include "chip_temperature_config.h"
#include "driver/temperature_sensor.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "chip_temperature";

static temperature_sensor_handle_t s_sensor;
static bool s_enabled;

esp_err_t chip_temperature_init(void)
{
    assert(s_sensor == NULL);

    const temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(
        CHIP_TEMPERATURE_RANGE_MIN_C, CHIP_TEMPERATURE_RANGE_MAX_C);
    ESP_RETURN_ON_ERROR(temperature_sensor_install(&config, &s_sensor), TAG,
                        "Sensor not installed");

    const esp_err_t err = temperature_sensor_enable(s_sensor);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Sensor not enabled (%s)", esp_err_to_name(err));
        chip_temperature_deinit();
        return err;
    }
    s_enabled = true;
    return ESP_OK;
}

void chip_temperature_deinit(void)
{
    if (s_enabled && (temperature_sensor_disable(s_sensor) != ESP_OK)) {
        ESP_LOGW(TAG, "Sensor not disabled");
    }
    s_enabled = false;
    if ((s_sensor != NULL) && (temperature_sensor_uninstall(s_sensor) != ESP_OK)) {
        ESP_LOGW(TAG, "Sensor not uninstalled");
    }
    s_sensor = NULL;
}

esp_err_t chip_temperature_read(float *out_celsius)
{
    assert(s_enabled);
    assert(out_celsius != NULL);
    return temperature_sensor_get_celsius(s_sensor, out_celsius);
}
