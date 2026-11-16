/**
 * @file gpio_output.h
 * @brief HAL of plain digital outputs (LEDs, relays): thin wrapper over the ESP-IDF GPIO driver.
 *
 * Replaced by a fake in the unit tests (test/fakes/gpio_output).
 */
#ifndef GPIO_OUTPUT_H
#define GPIO_OUTPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * @brief Configures @p gpio as a push-pull output at @p high, without interrupts.
 *
 * The level is set before the pad becomes an output, so an active-low load (a relay) never sees
 * a pulse of the wrong level.
 *
 * @param[in] gpio    GPIO number.
 * @param[in] pull_up Enables the internal pull-up (~45 kOhm), which holds the pad at a defined
 * level whenever it is not driven.
 * @param[in] high    Level the output starts at.
 * @return
 *  - ESP_OK
 *  - ESP_ERR_INVALID_ARG if @p gpio cannot be an output
 * @note Thread-safe for distinct GPIOs. Non-blocking.
 */
esp_err_t gpio_output_init(int32_t gpio, bool pull_up, bool high);

/**
 * @brief Returns @p gpio to its reset state (input with pull-up, output disabled).
 *
 * @param[in] gpio GPIO previously passed to gpio_output_init().
 * @note Thread-safe for distinct GPIOs. Non-blocking.
 */
void gpio_output_deinit(int32_t gpio);

/**
 * @brief Drives @p gpio high or low.
 *
 * @param[in] gpio GPIO configured by gpio_output_init().
 * @param[in] high true for the high level.
 * @return
 *  - ESP_OK
 *  - ESP_ERR_INVALID_ARG if @p gpio is not a valid output
 * @note Thread-safe for distinct GPIOs. Non-blocking.
 */
esp_err_t gpio_output_set_level(int32_t gpio, bool high);

#endif /* GPIO_OUTPUT_H */
