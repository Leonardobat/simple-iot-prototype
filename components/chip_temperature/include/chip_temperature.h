/**
 * @file chip_temperature.h
 * @brief HAL of the ESP32-S3 internal temperature sensor: thin wrapper over
 * driver/temperature_sensor.
 *
 * The sensor measures the silicon die, not the air: it follows temperature changes well but reads
 * above the ambient temperature, more so while Wi-Fi is active. Replaced by a fake in the unit
 * tests (test/fakes/chip_temperature).
 */
#ifndef CHIP_TEMPERATURE_H
#define CHIP_TEMPERATURE_H

#include "esp_err.h"

/**
 * @brief Installs and enables the sensor for the 10-40 degC range.
 *
 * @return
 *  - ESP_OK
 *  - errors of temperature_sensor_install() / temperature_sensor_enable() (nothing is left
 * installed)
 * @pre Not initialized (asserted).
 * @note Not thread-safe: call once at start-up.
 */
esp_err_t chip_temperature_init(void);

/**
 * @brief Disables and uninstalls the sensor.
 *
 * @post The component can be initialized again.
 * @note Not thread-safe. Safe to call when not initialized.
 */
void chip_temperature_deinit(void);

/**
 * @brief Reads the die temperature.
 *
 * @param[out] out_celsius Temperature in degrees Celsius; written only on ESP_OK.
 * @return
 *  - ESP_OK
 *  - ESP_FAIL if the conversion failed (e.g. the temperature is outside the hardware range)
 *  - other errors of temperature_sensor_get_celsius()
 * @pre Initialized; @p out_celsius is not NULL (asserted).
 * @note Not thread-safe (the driver has no lock): call from one task. Not ISR-safe.
 */
esp_err_t chip_temperature_read(float *out_celsius);

#endif /* CHIP_TEMPERATURE_H */
