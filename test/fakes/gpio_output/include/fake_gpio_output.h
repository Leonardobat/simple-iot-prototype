/**
 * @file fake_gpio_output.h
 * @brief Test double of gpio_output: records the level of each GPIO, returns scripted results.
 */
#ifndef FAKE_GPIO_OUTPUT_H
#define FAKE_GPIO_OUTPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "gpio_output.h"

/** GPIO0..GPIO48 of the ESP32-S3. */
#define FAKE_GPIO_COUNT 49U

/** Writes recorded by fake_gpio_output.log (the first FAKE_GPIO_LOG_SIZE successful ones). */
#define FAKE_GPIO_LOG_SIZE 32U

/** One successful gpio_output_set_level(). */
typedef struct {
    int32_t gpio;
    bool high;
} fake_gpio_write_t;

typedef struct {
    uint32_t init_calls;
    uint32_t deinit_calls;
    uint32_t set_calls;
    bool configured[FAKE_GPIO_COUNT]; /**< Between gpio_output_init() and gpio_output_deinit(). */
    bool high[FAKE_GPIO_COUNT];    /**< Last level of each GPIO (the initial level after init, false
                                      after deinit). */
    bool pull_up[FAKE_GPIO_COUNT]; /**< pull_up passed to the last gpio_output_init(). */
    fake_gpio_write_t log[FAKE_GPIO_LOG_SIZE]; /**< Successful writes, in order. */
    uint32_t log_len;                          /**< Entries of log in use. */
    esp_err_t init_result;                     /**< Returned by gpio_output_init(). */
    esp_err_t
        set_result; /**< Returned by gpio_output_set_level(); the level changes only on ESP_OK. */
} fake_gpio_output_t;

extern fake_gpio_output_t fake_gpio_output;

/** Clears every counter and level; results default to ESP_OK. */
void fake_gpio_output_reset(void);

#endif /* FAKE_GPIO_OUTPUT_H */
