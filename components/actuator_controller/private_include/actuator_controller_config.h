/**
 * @file actuator_controller_config.h
 * @brief Private configuration of the actuator_controller component (outputs of each actuator).
 */
#ifndef ACTUATOR_CONTROLLER_CONFIG_H
#define ACTUATOR_CONTROLLER_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#include "sdkconfig.h"

/** Colour of an RGB light whose "color" is missing or invalid: white, a little below full
 * brightness so it is clearly visible (~35 mA). 0xRRGGBB. */
#define ACTUATOR_DEFAULT_COLOR 0xF0F0F0U

/** "#rrggbb": '#' and six hex digits. */
#define ACTUATOR_COLOR_TEXT_LEN 7U
#define ACTUATOR_COLOR_HEX_BASE 16

/** Bit positions of each channel in a 0xRRGGBB colour. */
#define ACTUATOR_COLOR_RED_SHIFT    16U
#define ACTUATOR_COLOR_GREEN_SHIFT  8U
#define ACTUATOR_COLOR_CHANNEL_MASK 0xFFU

/** Packing of one actuator in an actuator_snapshot_t: the enabled flag, then the state (a switch
 * is 0 or 1, a curtain holds an actuator_curtain_t, an RGB light holds its on flag and 0xRRGGBB).
 */
#define ACTUATOR_SNAPSHOT_ENABLED_BIT 0x80000000U
#define ACTUATOR_SNAPSHOT_RGB_ON_BIT  0x01000000U
#define ACTUATOR_SNAPSHOT_COLOR_MASK  0x00FFFFFFU

/** GPIO of each plain LED; negative when the light has no output (simulated). */
#define ACTUATOR_LIGHT_1_GPIO ((int32_t)CONFIG_ACTUATOR_LIGHT_1_GPIO)
#define ACTUATOR_LIGHT_2_GPIO ((int32_t)CONFIG_ACTUATOR_LIGHT_2_GPIO)

/** Internal pull-up on the LED GPIOs. */
#if defined(CONFIG_ACTUATOR_LIGHT_PULL_UP)
#define ACTUATOR_LIGHT_PULL_UP true
#else
#define ACTUATOR_LIGHT_PULL_UP false
#endif

/** GPIO level that lights a plain LED. */
#if defined(CONFIG_ACTUATOR_LIGHT_ACTIVE_LOW)
#define ACTUATOR_LIGHT_ON_LEVEL false
#else
#define ACTUATOR_LIGHT_ON_LEVEL true
#endif

/** GPIO of each relay; negative when the actuator has no output (simulated). */
#define ACTUATOR_RELAY_1_GPIO      ((int32_t)CONFIG_ACTUATOR_RELAY_1_GPIO)
#define ACTUATOR_RELAY_2_GPIO      ((int32_t)CONFIG_ACTUATOR_RELAY_2_GPIO)
#define ACTUATOR_CURTAIN_UP_GPIO   ((int32_t)CONFIG_ACTUATOR_CURTAIN_UP_GPIO)
#define ACTUATOR_CURTAIN_DOWN_GPIO ((int32_t)CONFIG_ACTUATOR_CURTAIN_DOWN_GPIO)

/** GPIO level that switches a relay on; the internal pull-up holds the idle (off) level when the
 * relays are active low. */
#if defined(CONFIG_ACTUATOR_RELAY_ACTIVE_LOW)
#define ACTUATOR_RELAY_ON_LEVEL false
#define ACTUATOR_RELAY_PULL_UP  true
#else
#define ACTUATOR_RELAY_ON_LEVEL true
#define ACTUATOR_RELAY_PULL_UP  false
#endif

/** Pause between releasing one curtain direction and engaging the other. */
#define ACTUATOR_CURTAIN_REVERSE_DELAY_MS ((uint32_t)CONFIG_ACTUATOR_CURTAIN_REVERSE_DELAY_MS)

/** Text of the curtain command and state (the "command" field of the SPA and the readings). */
#define ACTUATOR_CURTAIN_TEXT_STOP "stop"
#define ACTUATOR_CURTAIN_TEXT_UP   "up"
#define ACTUATOR_CURTAIN_TEXT_DOWN "down"

#endif /* ACTUATOR_CONTROLLER_CONFIG_H */
