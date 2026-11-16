/**
 * @file fake_chip_temperature.h
 * @brief Test double of chip_temperature: returns a scripted temperature or error.
 */
#ifndef FAKE_CHIP_TEMPERATURE_H
#define FAKE_CHIP_TEMPERATURE_H

#include <stdint.h>

#include "chip_temperature.h"
#include "esp_err.h"

typedef struct {
    uint32_t init_calls;
    uint32_t deinit_calls;
    uint32_t read_calls;
    float celsius;         /**< Returned by chip_temperature_read() on ESP_OK. */
    esp_err_t init_result; /**< Returned by chip_temperature_init(). */
    esp_err_t read_result; /**< Returned by chip_temperature_read(). */
} fake_chip_temperature_t;

extern fake_chip_temperature_t fake_chip_temperature;

/** Clears every counter; results default to ESP_OK and the temperature to 25 degC. */
void fake_chip_temperature_reset(void);

#endif /* FAKE_CHIP_TEMPERATURE_H */
