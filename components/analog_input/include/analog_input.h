/**
 * @file analog_input.h
 * @brief HAL of analog inputs: thin wrapper over the ESP-IDF ADC one-shot driver with the ADC
 *        calibration driver (curve fitting on the ESP32-S3).
 *
 * Up to ANALOG_INPUT_MAX_INPUTS inputs at once, all on ADC1 (they share its unit): ADC2 cannot be
 * read while Wi-Fi is active, so a GPIO of ADC2 is refused. Replaced by a fake in the unit tests
 * (test/fakes/analog_input).
 */
#ifndef ANALOG_INPUT_H
#define ANALOG_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/** Inputs that can be open at the same time. */
#define ANALOG_INPUT_MAX_INPUTS 4U

/** One open analog input (opaque; storage comes from a static pool). */
typedef struct analog_input_s *analog_input_handle_t;

/**
 * @brief Opens the ADC1 channel of @p gpio (12 dB attenuation) with its calibration.
 *
 * The ADC unit is claimed by the first input and released with the last. Without calibration eFuses
 * on the chip (e.g. QEMU), readings fall back to an approximate linear conversion and a warning is
 * logged; see analog_input_is_calibrated().
 *
 * @param[in]  gpio       GPIO with an ADC1 channel.
 * @param[out] out_handle Receives the input; written only on ESP_OK.
 * @return
 *  - ESP_OK
 *  - ESP_ERR_NOT_FOUND / ESP_ERR_INVALID_ARG if @p gpio has no ADC channel
 *  - ESP_ERR_NOT_SUPPORTED if @p gpio is on ADC2
 *  - ESP_ERR_NO_MEM if ANALOG_INPUT_MAX_INPUTS inputs are already open
 *  - other errors of adc_oneshot_new_unit() / adc_oneshot_config_channel() (nothing is left
 *    claimed)
 * @pre @p out_handle is not NULL (asserted).
 * @note Not thread-safe: call at start-up.
 */
esp_err_t analog_input_init(int32_t gpio, analog_input_handle_t *out_handle);

/**
 * @brief Closes @p handle, and releases the ADC unit if it was the last input.
 *
 * @param[in] handle Input from analog_input_init(), or NULL (nothing to do).
 * @post @p handle is invalid; its slot can be reused.
 * @note Not thread-safe.
 */
void analog_input_deinit(analog_input_handle_t handle);

/**
 * @brief One one-shot conversion, in millivolts.
 *
 * @param[in]  handle Open input.
 * @param[out] out_mv Voltage at the pin; written only on ESP_OK.
 * @return
 *  - ESP_OK
 *  - ESP_ERR_TIMEOUT / other errors of adc_oneshot_read() or adc_oneshot_get_calibrated_result()
 * @pre @p handle is open and @p out_mv is not NULL (asserted).
 * @note Not thread-safe: call from one task. Not ISR-safe. Blocking for the conversion (µs).
 */
esp_err_t analog_input_read_mv(analog_input_handle_t handle, int32_t *out_mv);

/**
 * @brief Tells whether readings of @p handle use the chip calibration (false: approximate linear
 *        fallback).
 * @pre @p handle is open (asserted).
 */
bool analog_input_is_calibrated(analog_input_handle_t handle);

#endif /* ANALOG_INPUT_H */
