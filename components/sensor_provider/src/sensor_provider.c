#include "sensor_provider.h"

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "analog_input.h"
#include "chip_temperature.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "json_writer.h"
#include "ldr.h"
#include "lm35.h"
#include "mq135.h"
#include "sensor_provider_config.h"

static const char *TAG = "sensor_provider";

/* Reads the current value of one sensor; NaN when it cannot be read. Only called while enabled. */
typedef float (*sensor_reader_t)(void);

/* One sensor: what it is (published) and how it is read. */
typedef struct {
    sensor_info_t info;
    sensor_reader_t read;
} sensor_entry_t;

static float read_chip_temperature(void);
static float read_temperature_2(void);
static float read_luminosity_1(void);
static float read_luminosity_2(void);
static float read_gas(void);

/* Indexed by sensor_t. temperature_1 is the chip; the others read the ADC (LM35, LDRs, MQ-135) and
 * are simulated while their GPIO is -1. */
static const sensor_entry_t s_sensors[SENSOR_COUNT] = {
    [SENSOR_TEMPERATURE_1] = {{"temperature_1", "temperature", "\\u00b0C", false, 1U},
                              read_chip_temperature},
    [SENSOR_TEMPERATURE_2] = {{"temperature_2", "temperature", "\\u00b0C", !SENSOR_LM35_HAS_INPUT,
                               1U},
                              read_temperature_2},
    [SENSOR_LUMINOSITY_1] = {{"luminosity_1", "luminosity", "lx", !SENSOR_LDR_1_HAS_INPUT, 0U},
                             read_luminosity_1},
    [SENSOR_LUMINOSITY_2] = {{"luminosity_2", "luminosity", "lx", !SENSOR_LDR_2_HAS_INPUT, 0U},
                             read_luminosity_2},
    [SENSOR_GAS] = {{"gas_1", "gas", "ppm", !SENSOR_MQ135_HAS_INPUT, 0U}, read_gas},
};

static bool s_initialized;
static bool s_chip_ready;
static analog_input_handle_t s_ldr_1; /* NULL when luminosity_1 is simulated. */
static analog_input_handle_t s_ldr_2; /* NULL when luminosity_2 is simulated. */
static analog_input_handle_t s_lm35;  /* NULL when temperature_2 is simulated. */
static analog_input_handle_t s_mq135; /* NULL when gas_1 is simulated. */
/* Written by the device_listener task, read by the publishing task. */
static atomic_bool s_enabled[SENSOR_COUNT];

static float random_between(float min, float max)
{
    const float fraction = (float)esp_random() / (float)UINT32_MAX;
    return min + ((max - min) * fraction);
}

/* Opens the ADC input of a sensor; a negative GPIO means the sensor is simulated: nothing to open.
 */
static esp_err_t open_input(int32_t gpio, analog_input_handle_t *out_input)
{
    return (gpio < 0) ? ESP_OK : analog_input_init(gpio, out_input);
}

esp_err_t sensor_provider_init(void)
{
    assert(!s_initialized);

    s_initialized = true;
    for (uint32_t i = 0U; i < (uint32_t)SENSOR_COUNT; i++) {
        atomic_store(&s_enabled[i], false);
    }
    esp_err_t err = chip_temperature_init();
    s_chip_ready = err == ESP_OK;
    if (err == ESP_OK) {
        err = open_input(SENSOR_LDR_1_GPIO, &s_ldr_1);
    }
    if (err == ESP_OK) {
        err = open_input(SENSOR_LDR_2_GPIO, &s_ldr_2);
    }
    if (err == ESP_OK) {
        err = open_input(SENSOR_LM35_GPIO, &s_lm35);
    }
    if (err == ESP_OK) {
        err = open_input(SENSOR_MQ135_GPIO, &s_mq135);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Sensors not initialized (%s)", esp_err_to_name(err));
        sensor_provider_deinit();
    }
    return err;
}

void sensor_provider_deinit(void)
{
    /* NULL-safe: an input is not opened when simulated or after a failed init. */
    analog_input_deinit(s_mq135);
    analog_input_deinit(s_lm35);
    analog_input_deinit(s_ldr_2);
    analog_input_deinit(s_ldr_1);
    s_mq135 = NULL;
    s_lm35 = NULL;
    s_ldr_2 = NULL;
    s_ldr_1 = NULL;
    if (s_chip_ready) {
        chip_temperature_deinit();
    }
    s_chip_ready = false;
    s_initialized = false;
}

/* Reads `input` and converts its voltage at the pin; NaN (and a warning) when the read fails. */
static float read_converted(sensor_t sensor, analog_input_handle_t input,
                            float (*convert)(int32_t millivolts))
{
    int32_t millivolts = 0;
    const esp_err_t err = analog_input_read_mv(input, &millivolts);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s not read (%s)", s_sensors[sensor].info.id, esp_err_to_name(err));
        return NAN;
    }
    ESP_LOGD(TAG, "%s: %" PRId32 " mV", s_sensors[sensor].info.id, millivolts);
    return convert(millivolts);
}

/* Illuminance from an LDR, or a mock value when it is not wired (its GPIO is -1: no input). */
static float read_ldr(sensor_t sensor, analog_input_handle_t input)
{
    if (input == NULL) {
        return (float)(esp_random() % (SENSOR_MOCK_LUMINOSITY_MAX_LUX + 1U));
    }
    return read_converted(sensor, input, ldr_lux_from_mv);
}

static float read_luminosity_1(void)
{
    return read_ldr(SENSOR_LUMINOSITY_1, s_ldr_1);
}

static float read_luminosity_2(void)
{
    return read_ldr(SENSOR_LUMINOSITY_2, s_ldr_2);
}

/* Air temperature from the LM35, or a mock value when it is not wired (CONFIG_LM35_GPIO = -1). */
static float read_temperature_2(void)
{
    if (!SENSOR_LM35_HAS_INPUT) {
        return random_between(SENSOR_MOCK_TEMPERATURE_MIN_C, SENSOR_MOCK_TEMPERATURE_MAX_C);
    }
    return read_converted(SENSOR_TEMPERATURE_2, s_lm35, lm35_celsius_from_mv);
}

/* ppm of the MQ-135, logging Rs (at debug level) to help calibrate CONFIG_MQ135_R0_OHMS. */
static float gas_ppm_from_mv(int32_t millivolts)
{
    ESP_LOGD(TAG, "gas_1: Rs %.0f ohm", (double)mq135_resistance_ohms(millivolts));
    return mq135_ppm_from_mv(millivolts);
}

/* Gas concentration (CO2 equivalent) from the MQ-135, or a mock value when it is not wired
 * (CONFIG_MQ135_GPIO = -1). */
static float read_gas(void)
{
    if (!SENSOR_MQ135_HAS_INPUT) {
        return random_between(SENSOR_MOCK_GAS_MIN_PPM, SENSOR_MOCK_GAS_MAX_PPM);
    }
    return read_converted(SENSOR_GAS, s_mq135, gas_ppm_from_mv);
}

/* Die temperature, or NaN when the read fails. */
static float read_chip_temperature(void)
{
    float celsius = NAN;
    const esp_err_t err = chip_temperature_read(&celsius);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s not read (%s)", s_sensors[SENSOR_TEMPERATURE_1].info.id,
                 esp_err_to_name(err));
        return NAN;
    }
    return celsius;
}

const sensor_info_t *sensor_provider_info(sensor_t sensor)
{
    assert(sensor < SENSOR_COUNT);
    return &s_sensors[sensor].info;
}

void sensor_provider_set_enabled(sensor_t sensor, bool enabled)
{
    assert(sensor < SENSOR_COUNT);
    if (atomic_exchange(&s_enabled[sensor], enabled) != enabled) {
        ESP_LOGI(TAG, "%s %s", s_sensors[sensor].info.id, enabled ? "enabled" : "disabled");
    }
}

bool sensor_provider_is_enabled(sensor_t sensor)
{
    assert(sensor < SENSOR_COUNT);
    return atomic_load(&s_enabled[sensor]);
}

void sensor_provider_read(sensor_readings_t *out)
{
    assert(s_initialized);
    assert(out != NULL);

    for (uint32_t i = 0U; i < (uint32_t)SENSOR_COUNT; i++) {
        out->value[i] = sensor_provider_is_enabled((sensor_t)i) ? s_sensors[i].read() : NAN;
    }
}

/* Adds a sensor with a finite value: a disabled one reads NaN, and a stored reading keeps what it
 * had when it was sampled even if the sensor is disabled later. */
static void add_value(json_writer_t *writer, sensor_t sensor, float value)
{
    const sensor_info_t *info = &s_sensors[sensor].info;
    if (isfinite(value)) {
        /* printf takes the precision of "%.*f" as an int. */
        json_writer_add(writer, "\"%s\":%.*f", info->id, (int)info->decimals, (double)value);
    }
}

esp_err_t sensor_readings_to_json(const sensor_readings_t *readings, char *buffer, size_t size)
{
    assert(readings != NULL);

    json_writer_t writer;
    json_writer_begin(&writer, buffer, size);
    for (uint32_t i = 0U; i < (uint32_t)SENSOR_COUNT; i++) {
        add_value(&writer, (sensor_t)i, readings->value[i]);
    }
    return json_writer_end(&writer);
}

esp_err_t sensor_provider_catalog_to_json(char *buffer, size_t size)
{
    json_writer_t writer;
    json_writer_begin(&writer, buffer, size);
    for (uint32_t i = 0U; i < (uint32_t)SENSOR_COUNT; i++) {
        const sensor_info_t *info = &s_sensors[i].info;
        const json_catalog_item_t item = {info->id, info->type, info->unit, info->simulated};
        json_writer_add_catalog_item(&writer, &item);
    }
    return json_writer_end(&writer);
}
