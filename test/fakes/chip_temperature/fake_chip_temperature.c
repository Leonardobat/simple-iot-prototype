#include "fake_chip_temperature.h"

#include <string.h>

#define FAKE_DEFAULT_CELSIUS 25.0f

fake_chip_temperature_t fake_chip_temperature;

void fake_chip_temperature_reset(void)
{
    memset(&fake_chip_temperature, 0, sizeof(fake_chip_temperature));
    fake_chip_temperature.celsius = FAKE_DEFAULT_CELSIUS;
    fake_chip_temperature.init_result = ESP_OK;
    fake_chip_temperature.read_result = ESP_OK;
}

esp_err_t chip_temperature_init(void)
{
    fake_chip_temperature.init_calls++;
    return fake_chip_temperature.init_result;
}

void chip_temperature_deinit(void)
{
    fake_chip_temperature.deinit_calls++;
}

esp_err_t chip_temperature_read(float *out_celsius)
{
    fake_chip_temperature.read_calls++;
    if (fake_chip_temperature.read_result == ESP_OK) {
        *out_celsius = fake_chip_temperature.celsius;
    }
    return fake_chip_temperature.read_result;
}
