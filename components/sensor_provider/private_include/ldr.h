/**
 * @file ldr.h
 * @brief Converts the voltage of the LDR divider into an approximate illuminance (private).
 */
#ifndef LDR_H
#define LDR_H

#include <stdint.h>

/**
 * @brief Approximate illuminance from the divider voltage.
 *
 * R_ldr comes from the divider; lux = 10 * (R10 / R_ldr)^(1 / gamma), with the datasheet values
 * in Kconfig. An LDR is not a calibrated light meter: expect the right order of magnitude, not
 * an exact value.
 *
 * @param[in] millivolts Voltage at the ADC pin.
 * @return Lux in [0, SENSOR_LDR_MAX_LUX], or NaN if @p millivolts is outside [0, supply].
 * @note Pure function: thread-safe, non-blocking.
 */
float ldr_lux_from_mv(int32_t millivolts);

#endif /* LDR_H */
