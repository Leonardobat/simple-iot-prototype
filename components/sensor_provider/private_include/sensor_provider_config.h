/**
 * @file sensor_provider_config.h
 * @brief Private configuration of the sensor_provider component (mocked readings).
 */
#ifndef SENSOR_PROVIDER_CONFIG_H
#define SENSOR_PROVIDER_CONFIG_H

/** Mocked room temperature (temperature_2). */
#define SENSOR_MOCK_TEMPERATURE_MIN_C  18.0f
#define SENSOR_MOCK_TEMPERATURE_MAX_C  26.0f
#define SENSOR_MOCK_LUMINOSITY_MAX_LUX 1000U
#define SENSOR_MOCK_GAS_MIN_PPM        400.0f
#define SENSOR_MOCK_GAS_MAX_PPM        1000.0f

/* ---- MQ-135 (gas_1) ---- */

/** GPIO fed by the divider on the module output; negative when gas_1 is simulated. */
#define SENSOR_MQ135_GPIO      ((int32_t)CONFIG_MQ135_GPIO)
#define SENSOR_MQ135_HAS_INPUT (SENSOR_MQ135_GPIO >= 0)

/** The module (heater and load resistor) is fed from 5 V. */
#define SENSOR_MQ135_SUPPLY_MV 5000.0f
#define SENSOR_MQ135_LOAD_OHMS ((float)CONFIG_MQ135_LOAD_RESISTOR_OHMS)
#define SENSOR_MQ135_R0_OHMS   ((float)CONFIG_MQ135_R0_OHMS)
/** Module output = voltage at the pin * (top + bottom) / bottom of the divider. */
#define SENSOR_MQ135_DIVIDER_RATIO                                                                 \
    ((float)(CONFIG_MQ135_DIVIDER_TOP_OHMS + CONFIG_MQ135_DIVIDER_BOTTOM_OHMS) /                   \
     (float)CONFIG_MQ135_DIVIDER_BOTTOM_OHMS)
/** CO2 curve ppm = a * (Rs / R0)^-b. */
#define SENSOR_MQ135_CURVE_A 116.6020682f
#define SENSOR_MQ135_CURVE_B 2.769034857f
/** Readings above this (Rs far below R0) are reported as this value. */
#define SENSOR_MQ135_MAX_PPM 10000.0f

/* ---- LM35 (temperature_2) ---- */

/** GPIO of the LM35 output; negative when temperature_2 is simulated. */
#define SENSOR_LM35_GPIO      ((int32_t)CONFIG_LM35_GPIO)
#define SENSOR_LM35_HAS_INPUT (SENSOR_LM35_GPIO >= 0)

/** 10 mV per degree Celsius. */
#define SENSOR_LM35_MV_PER_C 10.0f
/** Single-supply range of the part is 0-150 degC; a higher voltage means a wiring fault. */
#define SENSOR_LM35_MAX_MV 1500

/* ---- LDR (luminosity_1) ---- */

#include <stdint.h>

#include "sdkconfig.h"

/** GPIO of each LDR divider (same divider values); negative when the luminosity is simulated. */
#define SENSOR_LDR_1_GPIO      ((int32_t)CONFIG_LDR_GPIO)
#define SENSOR_LDR_1_HAS_INPUT (SENSOR_LDR_1_GPIO >= 0)
#define SENSOR_LDR_2_GPIO      ((int32_t)CONFIG_LDR_2_GPIO)
#define SENSOR_LDR_2_HAS_INPUT (SENSOR_LDR_2_GPIO >= 0)

/** The divider is fed from the 3.3 V rail. */
#define SENSOR_LDR_SUPPLY_MV 3300

#define SENSOR_LDR_FIXED_OHMS ((float)CONFIG_LDR_FIXED_RESISTOR_OHMS)
#define SENSOR_LDR_R10_OHMS   ((float)CONFIG_LDR_RESISTANCE_AT_10_LUX_OHMS)
#define SENSOR_LDR_GAMMA      ((float)CONFIG_LDR_GAMMA_X100 / 100.0f)
#define SENSOR_LDR_REF_LUX    10.0f
/** Readings above this (LDR almost shorted) are reported as this value. */
#define SENSOR_LDR_MAX_LUX 100000.0f

#if defined(CONFIG_LDR_TO_SUPPLY)
#define SENSOR_LDR_TO_SUPPLY true
#else
#define SENSOR_LDR_TO_SUPPLY false
#endif

#endif /* SENSOR_PROVIDER_CONFIG_H */
