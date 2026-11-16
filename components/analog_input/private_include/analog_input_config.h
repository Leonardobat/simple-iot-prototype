/**
 * @file analog_input_config.h
 * @brief Private configuration of the analog_input component.
 */
#ifndef ANALOG_INPUT_CONFIG_H
#define ANALOG_INPUT_CONFIG_H

#include "hal/adc_types.h"

/** 12 dB attenuation: measures about 0 to 3.1 V, enough for a divider on the 3.3 V supply. */
#define ANALOG_INPUT_ATTEN ADC_ATTEN_DB_12

/** Only for chips without calibration eFuses: 12-bit raw full scale and its approximate voltage. */
#define ANALOG_INPUT_RAW_FULL_SCALE      4095
#define ANALOG_INPUT_UNCALIBRATED_MAX_MV 3100

#endif /* ANALOG_INPUT_CONFIG_H */
