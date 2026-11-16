/**
 * @file sensor_provider.h
 * @brief Catalog of the device sensors, their enabled state, readings and JSON for Firebase.
 *
 * Each sensor has a fixed id in the Realtime Database (devices/<id>/sensors/<sensorId>). The SPA
 * enables or disables it; disabled sensors are neither read nor published. temperature_1 is the
 * ESP32-S3 internal sensor (chip_temperature: die temperature, above ambient); temperature_2 is an
 * LM35 on CONFIG_LM35_GPIO, luminosity_1 / luminosity_2 are LDR dividers on CONFIG_LDR_GPIO /
 * CONFIG_LDR_2_GPIO and gas_1 an MQ-135 on CONFIG_MQ135_GPIO, all read by the ADC
 * (analog_input; each simulated when its GPIO is -1); the others are mocked (catalog field
 * "simulated").
 * See docs/adr/0001-modelo-de-dados.md and docs/adr/0003-sensores.md.
 */
#ifndef SENSOR_PROVIDER_H
#define SENSOR_PROVIDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/** Fits the JSON of any sensor_readings_t with every sensor enabled (-FLT_MAX, INT32_MIN). */
#define SENSOR_READINGS_JSON_MAX_LEN 384U

/** Fits the JSON of sensor_provider_catalog_to_json(). */
#define SENSOR_CATALOG_JSON_MAX_LEN 512U

/** Sensors of this device. */
typedef enum {
    SENSOR_TEMPERATURE_1 = 0, /**< Internal sensor of the chip (real). */
    SENSOR_TEMPERATURE_2,     /**< Air temperature (LM35 on the ADC; simulated without it). */
    SENSOR_LUMINOSITY_1,      /**< Illuminance (LDR on the ADC; simulated without it). */
    SENSOR_LUMINOSITY_2,      /**< Second LDR, simulated unless CONFIG_LDR_2_GPIO is set. */
    SENSOR_GAS,  /**< Gas (CO2 equivalent, ppm) from an MQ-135 on the ADC; simulated without it. */
    SENSOR_COUNT /**< Number of sensors, not a sensor. */
} sensor_t;

/** Fixed description of one sensor, as published in the catalog. */
typedef struct {
    const char *id;   /**< Database key, e.g. "temperature_1". */
    const char *type; /**< e.g. "temperature". */
    const char *unit; /**< JSON-escaped unit, e.g. "\\u00b0C"; NULL when the sensor has none. */
    bool simulated;   /**< true while the value comes from a mock instead of a driver. */
    uint8_t decimals; /**< Decimal places of the published number. */
} sensor_info_t;

/** One set of readings (plain data, copied by value). */
typedef struct {
    /** Value of each sensor in its unit (degC, lx, ppm), indexed by
     *  sensor_t. NaN when the sensor is disabled or could not be read. */
    float value[SENSOR_COUNT];
} sensor_readings_t;

/**
 * @brief Initializes the real sensors (chip_temperature, and analog_input for the LM35, the LDRs
 * and the MQ-135) and disables every sensor.
 *
 * @return
 *  - ESP_OK
 *  - errors of chip_temperature_init() and analog_input_init() (nothing is left initialized)
 * @pre Not initialized (asserted).
 * @note Not thread-safe: call once at start-up.
 */
esp_err_t sensor_provider_init(void);

/**
 * @brief Releases the real sensors.
 *
 * @post The component can be initialized again.
 * @note Not thread-safe. Safe to call on a partially initialized component.
 */
void sensor_provider_deinit(void);

/**
 * @brief Returns the fixed description of @p sensor.
 * @pre @p sensor < SENSOR_COUNT (asserted).
 * @note Thread-safe; the returned data is static and never changes.
 */
const sensor_info_t *sensor_provider_info(sensor_t sensor);

/**
 * @brief Enables or disables @p sensor (written by the SPA in the "enabled" field).
 * @pre @p sensor < SENSOR_COUNT (asserted).
 * @note Thread-safe (atomic), non-blocking. Every sensor starts disabled.
 */
void sensor_provider_set_enabled(sensor_t sensor, bool enabled);

/**
 * @brief Tells whether @p sensor is enabled.
 * @pre @p sensor < SENSOR_COUNT (asserted).
 * @note Thread-safe (atomic), non-blocking.
 */
bool sensor_provider_is_enabled(sensor_t sensor);

/**
 * @brief Reads every enabled sensor.
 *
 * A disabled sensor is not read, and one that fails (e.g. the chip is outside the 10-40 degC
 * range, or an ADC error) is logged; either way its value is NaN, which sensor_readings_to_json()
 * leaves out, so the other readings are still published.
 *
 * @param[out] out Receives the readings.
 * @pre Initialized; @p out is not NULL (asserted).
 * @note Call from one task (the internal sensor driver has no lock). Non-blocking.
 */
void sensor_provider_read(sensor_readings_t *out);

/**
 * @brief Serializes the sensors of @p readings that have a value, keyed by sensor id, e.g.
 *        {"temperature_1":22.4,"luminosity_1":320}.
 *
 * Numbers are written with the decimal places of sensor_info_t::decimals. Non-finite values (NaN,
 * infinity) are left out, since they are not valid JSON: that is how a disabled sensor stays out,
 * and a stored reading keeps the values it had when it was sampled. With no value the result is {}.
 *
 * @param[in]  readings Readings to serialize.
 * @param[out] buffer   Receives the NUL-terminated JSON (truncated on error).
 * @param[in]  size     Size of @p buffer; SENSOR_READINGS_JSON_MAX_LEN always fits.
 * @return
 *  - ESP_OK
 *  - ESP_ERR_INVALID_SIZE if @p buffer is too small.
 * @pre @p readings and @p buffer are not NULL, @p size > 0 (asserted).
 * @note Thread-safe, non-blocking.
 */
esp_err_t sensor_readings_to_json(const sensor_readings_t *readings, char *buffer, size_t size);

/**
 * @brief Serializes the catalog as a multi-path update of devices/<id>/sensors, e.g.
 *        {"temperature_1/type":"temperature","temperature_1/unit":"°C",
 *         "temperature_1/simulated":true,...}.
 *
 * Only the fields owned by the device are listed, so a PATCH keeps "enabled" (owned by the SPA).
 *
 * @param[out] buffer Receives the NUL-terminated JSON (truncated on error).
 * @param[in]  size   Size of @p buffer; SENSOR_CATALOG_JSON_MAX_LEN always fits.
 * @return
 *  - ESP_OK
 *  - ESP_ERR_INVALID_SIZE if @p buffer is too small.
 * @pre @p buffer is not NULL, @p size > 0 (asserted).
 * @note Thread-safe, non-blocking.
 */
esp_err_t sensor_provider_catalog_to_json(char *buffer, size_t size);

#endif /* SENSOR_PROVIDER_H */
