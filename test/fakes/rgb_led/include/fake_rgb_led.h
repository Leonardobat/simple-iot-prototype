/**
 * @file fake_rgb_led.h
 * @brief Test double of rgb_led: records the colour, returns scripted results.
 */
#ifndef FAKE_RGB_LED_H
#define FAKE_RGB_LED_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "rgb_led.h"

typedef struct {
    uint32_t init_calls;
    uint32_t deinit_calls;
    uint32_t set_calls;
    uint8_t red; /**< Last colour shown (0 after init and deinit). */
    uint8_t green;
    uint8_t blue;
    esp_err_t init_result; /**< Returned by rgb_led_init(). */
    esp_err_t set_result;  /**< Returned by rgb_led_set(); the colour changes only on ESP_OK. */
} fake_rgb_led_t;

extern fake_rgb_led_t fake_rgb_led;

/** Clears every counter and colour; results default to ESP_OK. */
void fake_rgb_led_reset(void);

#endif /* FAKE_RGB_LED_H */
