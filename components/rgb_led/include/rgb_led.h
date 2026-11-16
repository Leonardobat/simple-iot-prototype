/**
 * @file rgb_led.h
 * @brief HAL of the on-board addressable RGB LED (one WS2812 on CONFIG_RGB_LED_GPIO, driven by
 * RMT).
 *
 * Thin wrapper over the espressif/led_strip component; it is the only part of the light path
 * replaced by a fake in the unit tests (test/fakes/rgb_led), since QEMU has no RMT.
 */
#ifndef RGB_LED_H
#define RGB_LED_H

#include <stdint.h>

#include "esp_err.h"

/**
 * @brief Claims the RMT channel and the GPIO, and switches the LED off.
 *
 * @return
 *  - ESP_OK
 *  - ESP_ERR_NO_MEM / ESP_ERR_NOT_FOUND if no RMT channel or memory is available
 *  - ESP_ERR_INVALID_ARG if CONFIG_RGB_LED_GPIO cannot be an output
 *  - other led_strip errors (nothing is left allocated)
 * @pre Not initialized (asserted).
 * @note Not thread-safe: call once at start-up.
 */
esp_err_t rgb_led_init(void);

/**
 * @brief Switches the LED off and releases the RMT channel.
 *
 * @post The component can be initialized again.
 * @note Not thread-safe. Safe to call when not initialized.
 */
void rgb_led_deinit(void);

/**
 * @brief Shows one colour (0 = off for each channel).
 *
 * @param[in] red   Red level, 0..255.
 * @param[in] green Green level, 0..255.
 * @param[in] blue  Blue level, 0..255.
 * @return
 *  - ESP_OK
 *  - led_strip/RMT errors (the LED keeps its previous colour)
 * @pre Initialized (asserted).
 * @note Not thread-safe: call from one task at a time. Blocking for the ~30 us transfer.
 */
esp_err_t rgb_led_set(uint8_t red, uint8_t green, uint8_t blue);

#endif /* RGB_LED_H */
