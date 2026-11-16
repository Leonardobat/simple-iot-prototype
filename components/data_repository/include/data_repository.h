/**
 * @file data_repository.h
 * @brief Writes this device's data to the Realtime Database (docs/adr/0001-modelo-de-dados.md).
 */
#ifndef DATA_REPOSITORY_H
#define DATA_REPOSITORY_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/** Longest reading node ({"sensors":...,"actuators":...}), including the terminating NUL. */
#define DATA_REPOSITORY_READING_MAX_LEN 512U

/** Most readings data_repository_push_readings() sends in one request. */
#define DATA_REPOSITORY_BATCH_MAX 5U

/** One reading to write. */
typedef struct {
    int64_t timestamp_ms;     /**< Epoch time in milliseconds when it was sampled (the node key). */
    const char *sensors_json; /**< JSON object of sensor values keyed by sensor id. */
    const char *actuators_json; /**< JSON object of real actuator states keyed by actuator id. */
} data_repository_reading_t;

/**
 * @brief Writes @p count readings with one PATCH on readings/<DEVICE_ID>, one child per reading:
 *        {"<timestamp_ms>":{"sensors":<sensors_json>,"actuators":<actuators_json>},...}.
 *
 * Writing a reading again with the same timestamp replaces it, so a repeated request is safe.
 *
 * @param[in] readings Readings to write.
 * @param[in] count    Number of readings, 1 to DATA_REPOSITORY_BATCH_MAX.
 * @return
 *  - ESP_OK
 *  - ESP_ERR_INVALID_ARG if @p count is 0
 *  - ESP_ERR_INVALID_SIZE if @p count is above DATA_REPOSITORY_BATCH_MAX or the batch does not fit
 *    (DATA_REPOSITORY_READING_MAX_LEN per reading)
 *  - errors of firebase_rtdb_patch()
 * @pre firebase_rtdb is initialized; @p readings and their JSON pointers not NULL (asserted).
 * @note Not thread-safe: the request body lives in a shared buffer, so one task calls it. Blocking
 *       (network).
 */
esp_err_t data_repository_push_readings(const data_repository_reading_t *readings, size_t count);

/**
 * @brief Publishes the catalog with one PATCH per collection, on devices/<DEVICE_ID>/sensors and
 *        devices/<DEVICE_ID>/actuators; fields written by the SPA are kept.
 *
 * @param[in] sensors_json   Multi-path update from sensor_provider_catalog_to_json().
 * @param[in] actuators_json Multi-path update from actuator_controller_catalog_to_json().
 * @return
 *  - ESP_OK when both collections were written
 *  - errors of firebase_rtdb_patch() (the actuators are not written if the sensors failed)
 * @pre firebase_rtdb is initialized; pointers not NULL (asserted).
 * @note Thread-safe. Blocking (network).
 */
esp_err_t data_repository_publish_catalog(const char *sensors_json, const char *actuators_json);

#endif /* DATA_REPOSITORY_H */
