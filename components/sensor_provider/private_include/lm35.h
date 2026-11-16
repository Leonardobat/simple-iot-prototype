/**
 * @file lm35.h
 * @brief Converts the output voltage of an LM35 into degrees Celsius (private).
 */
#ifndef LM35_H
#define LM35_H

#include <stdint.h>

/**
 * @brief Temperature from the LM35 output (10 mV per degree Celsius, 0 V at 0 degC).
 *
 * @param[in] millivolts Voltage at the ADC pin.
 * @return Degrees Celsius, or NaN if @p millivolts is outside [0, SENSOR_LM35_MAX_MV] (the
 *         single-supply range of the part: 0-150 degC).
 * @note Pure function: thread-safe, non-blocking.
 */
float lm35_celsius_from_mv(int32_t millivolts);

#endif /* LM35_H */
