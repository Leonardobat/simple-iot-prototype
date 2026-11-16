/**
 * @file device_listener.h
 * @brief Keeps the device in sync with devices/<DEVICE_ID> (written by the SPA).
 *
 * A background task keeps a Realtime Database stream open on the device node, reconnecting with
 * exponential backoff. Every change to sensors/<id>/enabled, actuators/<id>/enabled,
 * actuators/<id>/command and, for RGB lights, actuators/<id>/color is applied to sensor_provider
 * and actuator_controller. The first event of every stream is the whole node, so a reconnect always
 * resynchronizes. A boolean that is missing or of another type counts as false; a missing or
 * invalid colour selects the default. See docs/adr/0001-modelo-de-dados.md.
 */
#ifndef DEVICE_LISTENER_H
#define DEVICE_LISTENER_H

#include "esp_err.h"

/**
 * @brief Starts the stream task.
 *
 * @return
 *  - ESP_OK
 *  - ESP_ERR_NO_MEM if the task or its semaphore cannot be created.
 * @pre Not initialized (asserted); firebase_rtdb is initialized.
 * @note Not thread-safe: call once at start-up.
 */
esp_err_t device_listener_init(void);

/**
 * @brief Stops the stream task and waits for it to exit.
 *
 * @return
 *  - ESP_OK (also when not initialized)
 *  - ESP_ERR_TIMEOUT if the task did not exit within 5 s (resources are kept).
 * @post On ESP_OK the component can be initialized again. Sensor and actuator states are kept.
 * @note Not thread-safe. Blocking: up to ~1 s normally.
 */
esp_err_t device_listener_deinit(void);

#endif /* DEVICE_LISTENER_H */
