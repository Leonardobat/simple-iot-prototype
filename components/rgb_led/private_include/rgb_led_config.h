/**
 * @file rgb_led_config.h
 * @brief Private configuration of the rgb_led component.
 */
#ifndef RGB_LED_CONFIG_H
#define RGB_LED_CONFIG_H

/** One LED on the board. */
#define RGB_LED_COUNT 1U

/** Index of the only LED. */
#define RGB_LED_INDEX 0U

/** RMT tick: 10 MHz (0.1 us) resolves the WS2812 timings. */
#define RGB_LED_RMT_RESOLUTION_HZ (10U * 1000U * 1000U)

#endif /* RGB_LED_CONFIG_H */
