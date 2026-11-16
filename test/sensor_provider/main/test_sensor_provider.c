#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "fake_analog_input.h"
#include "fake_chip_temperature.h"
#include "sensor_provider.h"
#include "unity.h"

/* Real sensor_provider over the fake chip_temperature (temperature_1) and analog_input
 * (temperature_2: LM35 on GPIO5; luminosity_1: LDR on GPIO4 with the default Kconfig divider:
 * 10 kOhm fixed to GND, 10 kOhm at 10 lux, gamma 0.7; gas_1: MQ-135 on GPIO8 with the default
 * Kconfig values: 10 k / 20 k divider, RL 10 kOhm, R0 76630 Ohm); the rest is mocked. */

#define LDR_GPIO   4
#define LM35_GPIO  5
#define MQ135_GPIO 8

#define CHIP_CELSIUS 31.5f

static char s_json[SENSOR_CATALOG_JSON_MAX_LEN];

static void set_all_enabled(bool enabled)
{
    for (uint32_t i = 0U; i < (uint32_t)SENSOR_COUNT; i++) {
        sensor_provider_set_enabled((sensor_t)i, enabled);
    }
}

void setUp(void)
{
    memset(s_json, 0, sizeof(s_json));
    fake_chip_temperature_reset();
    fake_chip_temperature.celsius = CHIP_CELSIUS;
    fake_analog_input_reset();
    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_provider_init());
}

void tearDown(void)
{
    sensor_provider_deinit();
}

/* ---- Lifecycle ---- */

TEST_CASE("test_sensor_provider_init_starts_chip_sensor_and_deinit_releases_it",
          "[sensor_provider]")
{
    TEST_ASSERT_EQUAL_UINT32(1U, fake_chip_temperature.init_calls);

    sensor_provider_deinit();
    TEST_ASSERT_EQUAL_UINT32(1U, fake_chip_temperature.deinit_calls);
    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_provider_init()); /* For tearDown. */
}

TEST_CASE("test_sensor_provider_init_failure_returns_error_and_reinit_works", "[sensor_provider]")
{
    sensor_provider_deinit();
    fake_chip_temperature.init_result = ESP_ERR_NOT_SUPPORTED;

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_NOT_SUPPORTED, sensor_provider_init());

    fake_chip_temperature.init_result = ESP_OK;
    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_provider_init());
}

TEST_CASE("test_sensor_provider_init_disables_every_sensor", "[sensor_provider]")
{
    set_all_enabled(true);
    sensor_provider_deinit();

    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_provider_init());
    TEST_ASSERT_FALSE(sensor_provider_is_enabled(SENSOR_TEMPERATURE_1));
}

/* ---- Chip temperature (temperature_1) ---- */

TEST_CASE("test_sensor_provider_read_takes_temperature_1_from_chip", "[sensor_provider]")
{
    sensor_readings_t readings = {0};
    sensor_provider_set_enabled(SENSOR_TEMPERATURE_1, true);

    sensor_provider_read(&readings);
    TEST_ASSERT_EQUAL_FLOAT(CHIP_CELSIUS, readings.value[SENSOR_TEMPERATURE_1]);
    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_readings_to_json(&readings, s_json, sizeof(s_json)));
    TEST_ASSERT_EQUAL_STRING("{\"temperature_1\":31.5}", s_json);
}

TEST_CASE("test_sensor_provider_disabled_chip_sensor_is_not_read", "[sensor_provider]")
{
    sensor_readings_t readings = {0};

    sensor_provider_read(&readings);
    TEST_ASSERT_EQUAL_UINT32(0U, fake_chip_temperature.read_calls);
    TEST_ASSERT_TRUE(isnan(readings.value[SENSOR_TEMPERATURE_1]));
}

TEST_CASE("test_sensor_provider_chip_read_error_leaves_only_that_sensor_out", "[sensor_provider]")
{
    sensor_readings_t readings = {0};
    fake_chip_temperature.read_result = ESP_FAIL; /* e.g. outside the 10-40 degC range. */
    sensor_provider_set_enabled(SENSOR_TEMPERATURE_1, true);
    sensor_provider_set_enabled(SENSOR_LUMINOSITY_2, true); /* Simulated: always has a value. */

    sensor_provider_read(&readings);
    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_readings_to_json(&readings, s_json, sizeof(s_json)));
    TEST_ASSERT_NULL(strstr(s_json, "temperature_1"));
    TEST_ASSERT_NOT_NULL(strstr(s_json, "luminosity_2"));
}

/* ---- LM35 (temperature_2) ---- */

/* Reads temperature_2 alone with the fake ADC at `millivolts` and returns its JSON. */
static const char *temperature_2_json_at(int32_t millivolts)
{
    sensor_readings_t readings = {0};
    fake_analog_input.channel[LM35_GPIO].millivolts = millivolts;
    sensor_provider_set_enabled(SENSOR_TEMPERATURE_2, true);
    sensor_provider_read(&readings);
    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_readings_to_json(&readings, s_json, sizeof(s_json)));
    return s_json;
}

TEST_CASE("test_sensor_provider_lm35_is_ten_millivolts_per_degree", "[sensor_provider]")
{
    TEST_ASSERT_EQUAL_STRING("{\"temperature_2\":25.0}", temperature_2_json_at(250));
}

TEST_CASE("test_sensor_provider_lm35_range_limits_are_accepted", "[sensor_provider]")
{
    TEST_ASSERT_EQUAL_STRING("{\"temperature_2\":0.0}", temperature_2_json_at(0));
    TEST_ASSERT_EQUAL_STRING("{\"temperature_2\":150.0}", temperature_2_json_at(1500));
}

TEST_CASE("test_sensor_provider_lm35_impossible_voltage_is_left_out", "[sensor_provider]")
{
    TEST_ASSERT_EQUAL_STRING("{}", temperature_2_json_at(1501));
    TEST_ASSERT_EQUAL_STRING("{}", temperature_2_json_at(-1));
}

TEST_CASE("test_sensor_provider_lm35_read_error_is_left_out", "[sensor_provider]")
{
    fake_analog_input.channel[LM35_GPIO].read_result = ESP_ERR_TIMEOUT;

    TEST_ASSERT_EQUAL_STRING("{}", temperature_2_json_at(250));
}

TEST_CASE("test_sensor_provider_disabled_lm35_is_not_read", "[sensor_provider]")
{
    sensor_readings_t readings = {0};

    sensor_provider_read(&readings);
    TEST_ASSERT_EQUAL_UINT32(0U, fake_analog_input.channel[LM35_GPIO].read_calls);
    TEST_ASSERT_TRUE(isnan(readings.value[SENSOR_TEMPERATURE_2]));
}

/* ---- MQ-135 (gas_1) ---- */

/* Reads gas_1 alone with the fake ADC at `millivolts` (at the pin) and returns its JSON. */
static const char *gas_json_at(int32_t millivolts)
{
    sensor_readings_t readings = {0};
    fake_analog_input.channel[MQ135_GPIO].millivolts = millivolts;
    sensor_provider_set_enabled(SENSOR_GAS, true);
    sensor_provider_read(&readings);
    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_readings_to_json(&readings, s_json, sizeof(s_json)));
    return s_json;
}

TEST_CASE("test_sensor_provider_mq135_clean_air_resistance_gives_curve_origin", "[sensor_provider]")
{
    /* 385 mV at the pin = 577 mV at AO (x1.5): Rs = 76.6 kOhm = R0, so ppm = a = 116.6. */
    TEST_ASSERT_EQUAL_STRING("{\"gas_1\":117}", gas_json_at(385));
}

TEST_CASE("test_sensor_provider_mq135_half_resistance_is_about_seven_times_more_gas",
          "[sensor_provider]")
{
    /* 690 mV at the pin: Rs = R0 / 2, so ppm = 116.6 * 2^2.769 = 795. */
    TEST_ASSERT_EQUAL_STRING("{\"gas_1\":795}", gas_json_at(690));
}

TEST_CASE("test_sensor_provider_mq135_very_low_resistance_saturates", "[sensor_provider]")
{
    TEST_ASSERT_EQUAL_STRING("{\"gas_1\":10000}", gas_json_at(3300));
}

TEST_CASE("test_sensor_provider_mq135_impossible_voltage_is_left_out", "[sensor_provider]")
{
    TEST_ASSERT_EQUAL_STRING("{}", gas_json_at(0));    /* No signal. */
    TEST_ASSERT_EQUAL_STRING("{}", gas_json_at(3334)); /* AO at or above the 5 V supply. */
}

TEST_CASE("test_sensor_provider_mq135_read_error_is_left_out", "[sensor_provider]")
{
    fake_analog_input.channel[MQ135_GPIO].read_result = ESP_ERR_TIMEOUT;

    TEST_ASSERT_EQUAL_STRING("{}", gas_json_at(385));
}

TEST_CASE("test_sensor_provider_disabled_mq135_is_not_read", "[sensor_provider]")
{
    sensor_readings_t readings = {0};

    sensor_provider_read(&readings);
    TEST_ASSERT_EQUAL_UINT32(0U, fake_analog_input.channel[MQ135_GPIO].read_calls);
    TEST_ASSERT_TRUE(isnan(readings.value[SENSOR_GAS]));
}

TEST_CASE("test_sensor_provider_mq135_init_failure_releases_chip_sensor_and_other_inputs",
          "[sensor_provider]")
{
    sensor_provider_deinit();
    fake_chip_temperature_reset();
    fake_analog_input_reset();
    fake_analog_input.init_fail_gpio = MQ135_GPIO;
    fake_analog_input.init_result = ESP_ERR_NOT_FOUND;

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_NOT_FOUND, sensor_provider_init());
    TEST_ASSERT_EQUAL_UINT32(1U, fake_chip_temperature.deinit_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, fake_analog_input.open_inputs);

    fake_analog_input.init_result = ESP_OK;
    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_provider_init()); /* For tearDown. */
}

TEST_CASE("test_sensor_provider_lm35_init_failure_releases_chip_sensor_and_ldr",
          "[sensor_provider]")
{
    sensor_provider_deinit();
    fake_chip_temperature_reset();
    fake_analog_input_reset();
    fake_analog_input.init_fail_gpio = LM35_GPIO;
    fake_analog_input.init_result = ESP_ERR_NOT_FOUND;

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_NOT_FOUND, sensor_provider_init());
    TEST_ASSERT_EQUAL_UINT32(1U, fake_chip_temperature.deinit_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, fake_analog_input.open_inputs);

    fake_analog_input.init_result = ESP_OK;
    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_provider_init()); /* For tearDown. */
}

/* ---- LDR (luminosity_1) ---- */

/* Reads luminosity_1 alone with the fake ADC at `millivolts` and returns its JSON. */
static const char *luminosity_json_at(int32_t millivolts)
{
    sensor_readings_t readings = {0};
    fake_analog_input.channel[LDR_GPIO].millivolts = millivolts;
    sensor_provider_set_enabled(SENSOR_LUMINOSITY_1, true);
    sensor_provider_read(&readings);
    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_readings_to_json(&readings, s_json, sizeof(s_json)));
    return s_json;
}

TEST_CASE("test_sensor_provider_init_claims_ldr_adc_input", "[sensor_provider]")
{
    TEST_ASSERT_EQUAL_UINT32(3U, fake_analog_input.init_calls); /* LDR, LM35 and MQ-135. */
    TEST_ASSERT_EQUAL_UINT32(3U, fake_analog_input.open_inputs);

    sensor_provider_deinit();
    TEST_ASSERT_EQUAL_UINT32(0U, fake_analog_input.open_inputs);
    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_provider_init()); /* For tearDown. */
}

TEST_CASE("test_sensor_provider_ldr_init_failure_releases_chip_sensor", "[sensor_provider]")
{
    sensor_provider_deinit();
    fake_chip_temperature_reset();
    fake_analog_input.init_result = ESP_ERR_NOT_FOUND;

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_NOT_FOUND, sensor_provider_init());
    TEST_ASSERT_EQUAL_UINT32(1U, fake_chip_temperature.deinit_calls);

    fake_analog_input.init_result = ESP_OK;
    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_provider_init()); /* For tearDown. */
}

TEST_CASE("test_sensor_provider_ldr_mid_scale_is_ten_lux", "[sensor_provider]")
{
    /* 1650 mV: LDR equals the 10 kOhm fixed resistor, its resistance at 10 lux. */
    TEST_ASSERT_EQUAL_STRING("{\"luminosity_1\":10}", luminosity_json_at(1650));
}

TEST_CASE("test_sensor_provider_ldr_more_voltage_is_more_light", "[sensor_provider]")
{
    /* 2200 mV: LDR at 5 kOhm -> 10 * 2^(1/0.7) = 26.9 lux. */
    TEST_ASSERT_EQUAL_STRING("{\"luminosity_1\":27}", luminosity_json_at(2200));
}

TEST_CASE("test_sensor_provider_ldr_zero_volts_is_dark", "[sensor_provider]")
{
    TEST_ASSERT_EQUAL_STRING("{\"luminosity_1\":0}", luminosity_json_at(0));
}

TEST_CASE("test_sensor_provider_ldr_supply_voltage_saturates", "[sensor_provider]")
{
    TEST_ASSERT_EQUAL_STRING("{\"luminosity_1\":100000}", luminosity_json_at(3300));
}

TEST_CASE("test_sensor_provider_ldr_impossible_voltage_is_left_out", "[sensor_provider]")
{
    TEST_ASSERT_EQUAL_STRING("{}", luminosity_json_at(3400));
    TEST_ASSERT_EQUAL_STRING("{}", luminosity_json_at(-1));
}

TEST_CASE("test_sensor_provider_ldr_read_error_is_left_out", "[sensor_provider]")
{
    fake_analog_input.channel[LDR_GPIO].read_result = ESP_ERR_TIMEOUT;

    TEST_ASSERT_EQUAL_STRING("{}", luminosity_json_at(1650));
}

TEST_CASE("test_sensor_provider_luminosity_2_is_simulated_without_an_adc_input",
          "[sensor_provider]")
{
    sensor_readings_t readings = {0};
    sensor_provider_set_enabled(SENSOR_LUMINOSITY_2, true);

    sensor_provider_read(&readings);
    TEST_ASSERT_TRUE(readings.value[SENSOR_LUMINOSITY_2] >= 0.0f);
    TEST_ASSERT_TRUE(readings.value[SENSOR_LUMINOSITY_2] <= 1000.0f); /* Mock range. */
    TEST_ASSERT_EQUAL_UINT32(3U, fake_analog_input.init_calls);       /* No input for it. */
}

TEST_CASE("test_sensor_provider_disabled_ldr_is_not_read", "[sensor_provider]")
{
    sensor_readings_t readings = {0};

    sensor_provider_read(&readings);
    TEST_ASSERT_EQUAL_UINT32(0U, fake_analog_input.channel[LDR_GPIO].read_calls);
    TEST_ASSERT_TRUE(isnan(readings.value[SENSOR_LUMINOSITY_1]));
}

TEST_CASE("test_sensor_provider_catalog_marks_ldr_real", "[sensor_provider]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_provider_catalog_to_json(s_json, sizeof(s_json)));

    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"luminosity_1/simulated\":false"));
}

/* ---- Enabled state and JSON ---- */

TEST_CASE("test_sensor_provider_sensors_start_disabled", "[sensor_provider]")
{
    for (uint32_t i = 0U; i < (uint32_t)SENSOR_COUNT; i++) {
        TEST_ASSERT_FALSE(sensor_provider_is_enabled((sensor_t)i));
    }
}

TEST_CASE("test_sensor_provider_set_enabled_is_reported", "[sensor_provider]")
{
    sensor_provider_set_enabled(SENSOR_GAS, true);

    TEST_ASSERT_TRUE(sensor_provider_is_enabled(SENSOR_GAS));
    TEST_ASSERT_FALSE(sensor_provider_is_enabled(SENSOR_TEMPERATURE_1));
}

TEST_CASE("test_sensor_provider_info_ids_are_unique", "[sensor_provider]")
{
    for (uint32_t i = 0U; i < (uint32_t)SENSOR_COUNT; i++) {
        for (uint32_t j = i + 1U; j < (uint32_t)SENSOR_COUNT; j++) {
            TEST_ASSERT_NOT_EQUAL_INT32(0, strcmp(sensor_provider_info((sensor_t)i)->id,
                                                  sensor_provider_info((sensor_t)j)->id));
        }
    }
}

TEST_CASE("test_sensor_provider_to_json_all_enabled_writes_every_sensor", "[sensor_provider]")
{
    const sensor_readings_t readings = {.value = {
                                            [SENSOR_TEMPERATURE_1] = 33.0f,
                                            [SENSOR_TEMPERATURE_2] = 22.44f,
                                            [SENSOR_LUMINOSITY_1] = 320.0f,
                                            [SENSOR_LUMINOSITY_2] = 15.0f,
                                            [SENSOR_GAS] = 812.4f,
                                        }};
    set_all_enabled(true);

    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_readings_to_json(&readings, s_json, sizeof(s_json)));
    TEST_ASSERT_EQUAL_STRING("{\"temperature_1\":33.0,\"temperature_2\":22.4,\"luminosity_1\":320,"
                             "\"luminosity_2\":15,\"gas_1\":812}",
                             s_json);
}

/* A reading with no value: what sensor_provider_read() gives for sensors that are off. */
static sensor_readings_t all_nan(void)
{
    sensor_readings_t readings = {0};
    for (uint32_t i = 0U; i < (uint32_t)SENSOR_COUNT; i++) {
        readings.value[i] = NAN;
    }
    return readings;
}

TEST_CASE("test_sensor_provider_to_json_all_nan_writes_empty_object", "[sensor_provider]")
{
    const sensor_readings_t readings = all_nan();

    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_readings_to_json(&readings, s_json, sizeof(s_json)));
    TEST_ASSERT_EQUAL_STRING("{}", s_json);
}

TEST_CASE("test_sensor_provider_to_json_writes_only_the_sensors_with_a_value", "[sensor_provider]")
{
    sensor_readings_t readings = all_nan();
    readings.value[SENSOR_TEMPERATURE_1] = 21.0f;
    readings.value[SENSOR_LUMINOSITY_2] = 15.0f;

    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_readings_to_json(&readings, s_json, sizeof(s_json)));
    TEST_ASSERT_EQUAL_STRING("{\"temperature_1\":21.0,\"luminosity_2\":15}", s_json);
}

TEST_CASE("test_sensor_provider_to_json_keeps_the_value_of_a_sensor_disabled_after_sampling",
          "[sensor_provider]")
{
    sensor_readings_t readings = all_nan();
    readings.value[SENSOR_TEMPERATURE_1] = 21.0f;
    sensor_provider_set_enabled(SENSOR_TEMPERATURE_1, false);

    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_readings_to_json(&readings, s_json, sizeof(s_json)));
    TEST_ASSERT_EQUAL_STRING("{\"temperature_1\":21.0}", s_json);
}

TEST_CASE("test_sensor_provider_to_json_negative_temperature_keeps_sign", "[sensor_provider]")
{
    sensor_readings_t readings = all_nan();
    readings.value[SENSOR_TEMPERATURE_1] = -5.4f;

    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_readings_to_json(&readings, s_json, sizeof(s_json)));
    TEST_ASSERT_EQUAL_STRING("{\"temperature_1\":-5.4}", s_json);
}

TEST_CASE("test_sensor_provider_to_json_non_finite_value_is_left_out", "[sensor_provider]")
{
    sensor_readings_t readings = all_nan();
    readings.value[SENSOR_LUMINOSITY_2] = INFINITY;

    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_readings_to_json(&readings, s_json, sizeof(s_json)));
    TEST_ASSERT_EQUAL_STRING("{}", s_json);
}

TEST_CASE("test_sensor_provider_to_json_worst_case_fits_max_len", "[sensor_provider]")
{
    sensor_readings_t readings = {0};
    for (uint32_t i = 0U; i < (uint32_t)SENSOR_COUNT; i++) {
        readings.value[i] = -FLT_MAX;
    }
    char json[SENSOR_READINGS_JSON_MAX_LEN] = {0};
    set_all_enabled(true);

    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_readings_to_json(&readings, json, sizeof(json)));
}

TEST_CASE("test_sensor_provider_to_json_small_buffer_returns_invalid_size", "[sensor_provider]")
{
    const sensor_readings_t readings = {0};
    char small[16] = {0};
    set_all_enabled(true);

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_SIZE,
                            sensor_readings_to_json(&readings, small, sizeof(small)));
    TEST_ASSERT_LESS_THAN_UINT32(sizeof(small), (uint32_t)strlen(small));
}

TEST_CASE("test_sensor_provider_read_returns_serializable_readings", "[sensor_provider]")
{
    sensor_readings_t readings = {0};
    set_all_enabled(true);

    sensor_provider_read(&readings);
    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_readings_to_json(&readings, s_json, sizeof(s_json)));
}

TEST_CASE("test_sensor_provider_catalog_lists_device_fields_only", "[sensor_provider]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_provider_catalog_to_json(s_json, sizeof(s_json)));

    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"temperature_1/type\":\"temperature\","
                                        "\"temperature_1/unit\":\"\\u00b0C\","
                                        "\"temperature_1/simulated\":false"));
    TEST_ASSERT_NULL(strstr(s_json, "enabled"));
}

TEST_CASE("test_sensor_provider_catalog_marks_temperatures_and_ldr_real", "[sensor_provider]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_provider_catalog_to_json(s_json, sizeof(s_json)));

    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"temperature_1/simulated\":false"));
    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"temperature_2/type\":\"temperature\""));
    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"temperature_2/simulated\":false"));
    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"luminosity_2/simulated\":true"));
    TEST_ASSERT_NOT_NULL(strstr(s_json, "\"gas_1/type\":\"gas\",\"gas_1/unit\":\"ppm\","
                                        "\"gas_1/simulated\":false"));
}

TEST_CASE("test_sensor_provider_catalog_fits_max_len", "[sensor_provider]")
{
    char json[SENSOR_CATALOG_JSON_MAX_LEN] = {0};

    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_provider_catalog_to_json(json, sizeof(json)));
}

TEST_CASE("test_sensor_provider_catalog_small_buffer_returns_invalid_size", "[sensor_provider]")
{
    char small[32] = {0};

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_SIZE,
                            sensor_provider_catalog_to_json(small, sizeof(small)));
}
