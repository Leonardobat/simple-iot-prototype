/**
 * @file config_store.h
 * @brief Keeps in flash which sensors and actuators are enabled, so a reset does not lose it
 *        (docs/adr/0004-persistencia-offline.md).
 *
 * The enabled flags are bit masks indexed by sensor_t and actuator_t (bit i = item i). The command
 * of an actuator is deliberately not kept: after a reset every actuator is off until the SPA says
 * otherwise.
 */
#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include <stdint.h>

#include "esp_err.h"

/** Enabled flags of every sensor and actuator. */
typedef struct {
    uint32_t sensors;   /**< Bit i set: sensor i is enabled. */
    uint32_t actuators; /**< Bit i set: actuator i is enabled. */
} config_store_enabled_t;

/**
 * @brief Reads the saved flags.
 *
 * @param[out] out Receives the flags; all clear when nothing valid is saved.
 * @return
 *  - ESP_OK
 *  - ESP_ERR_NOT_FOUND if nothing is saved yet (@p out is all clear)
 *  - ESP_ERR_INVALID_RESPONSE if the saved data is corrupt or of another version (@p out is all
 *    clear)
 *  - errors of flash_fs_read()
 * @pre flash_fs is mounted; @p out not NULL (asserted).
 * @note Thread-safe. Blocking.
 */
esp_err_t config_store_load(config_store_enabled_t *out);

/**
 * @brief Saves @p enabled atomically (a power cut leaves the previous flags or the new ones).
 *
 * @param[in] enabled Flags to keep.
 * @return
 *  - ESP_OK
 *  - errors of flash_fs_replace()
 * @pre flash_fs is mounted; @p enabled not NULL (asserted).
 * @note Thread-safe. Blocking (flash write): call it only when a flag changes.
 */
esp_err_t config_store_save(const config_store_enabled_t *enabled);

#endif /* CONFIG_STORE_H */
