/**
 * @file fake_analog_input.h
 * @brief Test double of analog_input: returns a scripted voltage or error per GPIO.
 */
#ifndef FAKE_ANALOG_INPUT_H
#define FAKE_ANALOG_INPUT_H

#include <stdint.h>

#include "analog_input.h"
#include "esp_err.h"

/** GPIOs 0 .. FAKE_ANALOG_INPUT_GPIO_COUNT - 1 can be opened. */
#define FAKE_ANALOG_INPUT_GPIO_COUNT 49U

/** Scripted behaviour of the input opened on one GPIO. */
typedef struct {
    uint32_t read_calls;
    int32_t millivolts;    /**< Returned by analog_input_read_mv() on ESP_OK. */
    esp_err_t read_result; /**< Returned by analog_input_read_mv(). */
} fake_analog_channel_t;

typedef struct {
    uint32_t init_calls;
    uint32_t deinit_calls;
    uint32_t open_inputs;   /**< Inputs opened and not yet closed. */
    esp_err_t init_result;  /**< Returned by analog_input_init() for init_fail_gpio. */
    int32_t init_fail_gpio; /**< GPIO that gets init_result; -1: every GPIO. */
    fake_analog_channel_t channel[FAKE_ANALOG_INPUT_GPIO_COUNT]; /**< Indexed by GPIO. */
} fake_analog_input_t;

extern fake_analog_input_t fake_analog_input;

/** Clears every counter; results default to ESP_OK and every voltage to 1650 mV (mid-scale). */
void fake_analog_input_reset(void);

#endif /* FAKE_ANALOG_INPUT_H */
