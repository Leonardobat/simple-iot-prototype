/**
 * @file actuator_controller.h
 * @brief Catalog of the device actuators, their enabled/command state and JSON for Firebase.
 *
 * Each actuator has a fixed id in the Realtime Database (devices/<id>/actuators/<actuatorId>). The
 * SPA writes "enabled" and "command"; an actuator is on only when both are true. The real state
 * (what the output driver last applied) is published with every reading.
 *
 * Outputs: light_1 and light_2 (type "light") are plain LEDs on CONFIG_ACTUATOR_LIGHT_1_GPIO /
 * CONFIG_ACTUATOR_LIGHT_2_GPIO and relay_1 and relay_2 (type "relay") are relay modules on
 * CONFIG_ACTUATOR_RELAY_1_GPIO / CONFIG_ACTUATOR_RELAY_2_GPIO (gpio_output); either is simulated
 * when its GPIO is -1 (catalog field "simulated"). rgb_light_1 (type "rgb_light") is the on-board
 * RGB LED (rgb_led), shown in the colour of its "color" field ("#rrggbb"; white "#f0f0f0" when
 * missing or invalid). curtain_1 (type "curtain") drives two relays, one per direction, and its
 * "command" is the text "up", "down" or "stop" (anything else, or missing, is "stop"); the two
 * relays are never on together, and a reversal waits CONFIG_ACTUATOR_CURTAIN_REVERSE_DELAY_MS.
 * See docs/adr/0001-modelo-de-dados.md.
 */
#ifndef ACTUATOR_CONTROLLER_H
#define ACTUATOR_CONTROLLER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/** Fits the JSON of actuator_controller_states_to_json() with every actuator enabled. */
#define ACTUATOR_STATES_JSON_MAX_LEN 192U

/** Fits the JSON of actuator_controller_catalog_to_json(). */
#define ACTUATOR_CATALOG_JSON_MAX_LEN 384U

/** Actuators of this device. */
typedef enum {
    ACTUATOR_LIGHT_1 = 0,
    ACTUATOR_LIGHT_2,
    ACTUATOR_RGB_LIGHT_1,
    ACTUATOR_CURTAIN_1,
    ACTUATOR_RELAY_1,
    ACTUATOR_RELAY_2,
    ACTUATOR_COUNT /**< Number of actuators, not an actuator. */
} actuator_t;

/** What the "command" of an actuator looks like and which extra fields it has. */
typedef enum {
    ACTUATOR_KIND_SWITCH = 0, /**< "command" is true/false (lights, relays). */
    ACTUATOR_KIND_RGB,        /**< Like a switch, plus a "color". */
    ACTUATOR_KIND_CURTAIN,    /**< "command" is "up", "down" or "stop". */
} actuator_kind_t;

/** Direction of a curtain. */
typedef enum {
    ACTUATOR_CURTAIN_STOP = 0,
    ACTUATOR_CURTAIN_UP,
    ACTUATOR_CURTAIN_DOWN,
} actuator_curtain_t;

/** Fixed description of one actuator, as published in the catalog. */
typedef struct {
    const char *id;       /**< Database key, e.g. "light_1". */
    const char *type;     /**< e.g. "light". */
    bool simulated;       /**< true while the output is a mock instead of a driver. */
    actuator_kind_t kind; /**< Shape of its "command". */
} actuator_info_t;

/**
 * @brief Initializes the outputs and switches every actuator off (disabled, command off).
 *
 * @return
 *  - ESP_OK
 *  - errors of rgb_led_init() and gpio_output_init() (nothing is left initialized)
 * @pre Not initialized (asserted).
 * @note Not thread-safe: call once at start-up, before device_listener_init().
 */
esp_err_t actuator_controller_init(void);

/**
 * @brief Switches the outputs off and releases them.
 *
 * @post The component can be initialized again.
 * @note Not thread-safe. Safe to call on a partially initialized component.
 */
void actuator_controller_deinit(void);

/**
 * @brief Returns the fixed description of @p actuator.
 * @pre @p actuator < ACTUATOR_COUNT (asserted).
 * @note Thread-safe; the returned data is static and never changes.
 */
const actuator_info_t *actuator_controller_info(actuator_t actuator);

/**
 * @brief Enables or disables @p actuator ("enabled" field). A disabled actuator is switched off.
 * @pre Initialized; @p actuator < ACTUATOR_COUNT (asserted).
 * @note Call from one task at a time (device_listener); readers in other tasks are safe (atomics).
 *       Drives the output (RGB transfer ~30 us). Every actuator starts disabled.
 */
void actuator_controller_set_enabled(actuator_t actuator, bool enabled);

/**
 * @brief Records the state requested for @p actuator ("command" field) and applies it.
 * @pre Initialized; @p actuator < ACTUATOR_COUNT and not a curtain (asserted).
 * @note Same threading as actuator_controller_set_enabled(). Kept off while the actuator is
 *       disabled.
 */
void actuator_controller_set_command(actuator_t actuator, bool on);

/**
 * @brief Records the direction requested for a curtain ("command" field) and applies it.
 *
 * Changing direction releases the running relay first, waits
 * CONFIG_ACTUATOR_CURTAIN_REVERSE_DELAY_MS and only then engages the other, so the two are never
 * on together. If a driver call fails, both relays are switched off and the state is "stop".
 *
 * @param[in] command "up", "down" or "stop". "" (field missing) means "stop"; any other value is
 *                    logged and also means "stop".
 * @pre Initialized; @p actuator < ACTUATOR_COUNT and a curtain; @p command not NULL (asserted).
 * @note Same threading as actuator_controller_set_enabled(). Blocks for the reverse delay.
 */
void actuator_controller_set_curtain(actuator_t actuator, const char *command);

/**
 * @brief Returns the direction a curtain is really moving in; stop before any command.
 * @pre @p actuator < ACTUATOR_COUNT and a curtain (asserted).
 * @note Thread-safe (atomic), non-blocking.
 */
actuator_curtain_t actuator_controller_get_curtain(actuator_t actuator);

/**
 * @brief Records the colour requested for @p actuator ("color" field) and shows it while on.
 *
 * @param[in] color "#rrggbb" in either case, e.g. "#ff8000". "" (field missing) or an invalid value
 *                  selects the default colour (white, "#f0f0f0"); an invalid one is logged.
 * @pre Initialized; @p actuator < ACTUATOR_COUNT and an RGB light; @p color not NULL (asserted).
 * @note Same threading as actuator_controller_set_enabled().
 */
void actuator_controller_set_color(actuator_t actuator, const char *color);

/**
 * @brief Returns the colour (0xRRGGBB) the RGB output last showed; the default before any.
 * @pre @p actuator < ACTUATOR_COUNT and an RGB light (asserted).
 * @note Thread-safe (atomic), non-blocking.
 */
uint32_t actuator_controller_get_color(actuator_t actuator);

/**
 * @brief Tells whether @p actuator is enabled.
 * @pre @p actuator < ACTUATOR_COUNT (asserted).
 * @note Thread-safe (atomic), non-blocking.
 */
bool actuator_controller_is_enabled(actuator_t actuator);

/**
 * @brief Returns the real state of @p actuator: what its output last applied (on only when enabled
 *        and commanded on, and the driver succeeded). A curtain is on while it is moving.
 * @pre @p actuator < ACTUATOR_COUNT (asserted).
 * @note Thread-safe (atomic), non-blocking.
 */
bool actuator_controller_is_on(actuator_t actuator);

/** Real state of every actuator at one instant, one opaque word each (enabled flag and state).
 * Plain data: it can be stored and serialized later with actuator_snapshot_to_json(). */
typedef struct {
    uint32_t state[ACTUATOR_COUNT];
} actuator_snapshot_t;

/**
 * @brief Captures the real state of every actuator.
 *
 * @param[out] out Receives the snapshot.
 * @pre @p out is not NULL (asserted).
 * @note Thread-safe, non-blocking.
 */
void actuator_controller_snapshot(actuator_snapshot_t *out);

/**
 * @brief Serializes the enabled actuators of @p snapshot, keyed by id, e.g.
 *        {"light_1":true,"rgb_light_1":{"on":true,"color":"#ff8000"},"curtain_1":"up"}. RGB lights
 *        report the colour they last showed, and curtains "up", "down" or "stop".
 *
 * @param[in]  snapshot Snapshot from actuator_controller_snapshot(); a corrupt curtain state is
 *                      written as "stop".
 * @param[out] buffer   Receives the NUL-terminated JSON (truncated on error).
 * @param[in]  size     Size of @p buffer; ACTUATOR_STATES_JSON_MAX_LEN always fits.
 * @return
 *  - ESP_OK
 *  - ESP_ERR_INVALID_SIZE if @p buffer is too small.
 * @pre Pointers not NULL, @p size > 0 (asserted).
 * @note Thread-safe, non-blocking.
 */
esp_err_t actuator_snapshot_to_json(const actuator_snapshot_t *snapshot, char *buffer, size_t size);

/**
 * @brief actuator_controller_snapshot() followed by actuator_snapshot_to_json().
 *
 * @param[out] buffer Receives the NUL-terminated JSON (truncated on error).
 * @param[in]  size   Size of @p buffer; ACTUATOR_STATES_JSON_MAX_LEN always fits.
 * @return
 *  - ESP_OK
 *  - ESP_ERR_INVALID_SIZE if @p buffer is too small.
 * @pre @p buffer is not NULL, @p size > 0 (asserted).
 * @note Thread-safe, non-blocking.
 */
esp_err_t actuator_controller_states_to_json(char *buffer, size_t size);

/**
 * @brief Serializes the catalog as a multi-path update of devices/<id>/actuators, e.g.
 *        {"light_1/type":"light","light_1/simulated":true,...}.
 *
 * Only the fields owned by the device are listed, so a PATCH keeps "enabled" and "command".
 *
 * @param[out] buffer Receives the NUL-terminated JSON (truncated on error).
 * @param[in]  size   Size of @p buffer; ACTUATOR_CATALOG_JSON_MAX_LEN always fits.
 * @return
 *  - ESP_OK
 *  - ESP_ERR_INVALID_SIZE if @p buffer is too small.
 * @pre @p buffer is not NULL, @p size > 0 (asserted).
 * @note Thread-safe, non-blocking.
 */
esp_err_t actuator_controller_catalog_to_json(char *buffer, size_t size);

#endif /* ACTUATOR_CONTROLLER_H */
