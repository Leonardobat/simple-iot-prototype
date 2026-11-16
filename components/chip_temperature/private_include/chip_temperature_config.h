/**
 * @file chip_temperature_config.h
 * @brief Private configuration of the chip_temperature component.
 */
#ifndef CHIP_TEMPERATURE_CONFIG_H
#define CHIP_TEMPERATURE_CONFIG_H

/** Expected range in degrees Celsius: the driver picks the most accurate hardware range covering
 * it. Readings outside the hardware range fail (ESP_FAIL). */
#define CHIP_TEMPERATURE_RANGE_MIN_C 10
#define CHIP_TEMPERATURE_RANGE_MAX_C 40

#endif /* CHIP_TEMPERATURE_CONFIG_H */
