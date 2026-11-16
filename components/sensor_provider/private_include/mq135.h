/**
 * @file mq135.h
 * @brief Converts the output voltage of an MQ-135 gas sensor module into an approximate CO2
 *        concentration (private).
 */
#ifndef MQ135_H
#define MQ135_H

#include <stdint.h>

/**
 * @brief Sensor resistance Rs from the voltage at the ADC pin.
 *
 * The module output goes through a voltage divider (the ADC tolerates less than the 5 V of the
 * module) and a load resistor RL: Rs = RL * (supply / Vout - 1).
 *
 * @param[in] millivolts Voltage at the ADC pin.
 * @return Rs in ohms, or NaN if the module output is outside (0, supply).
 * @note Pure function: thread-safe, non-blocking.
 */
float mq135_resistance_ohms(int32_t millivolts);

/**
 * @brief Approximate CO2-equivalent concentration from the voltage at the ADC pin.
 *
 * ppm = a * (Rs / R0)^-b, the CO2 curve most used for this part, where Rs comes from
 * mq135_resistance_ohms() and R0 is Rs in clean air (Kconfig). An MQ-135 reacts to
 * several gases and drifts with temperature, humidity and age: expect the order of magnitude and
 * the trend, not a calibrated value.
 *
 * @param[in] millivolts Voltage at the ADC pin.
 * @return ppm in [0, SENSOR_MQ135_MAX_PPM], or NaN as mq135_resistance_ohms().
 * @note Pure function: thread-safe, non-blocking.
 */
float mq135_ppm_from_mv(int32_t millivolts);

#endif /* MQ135_H */
