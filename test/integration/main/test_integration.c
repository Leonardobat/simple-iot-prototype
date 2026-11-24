#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "actuator_controller.h"
#include "analog_input.h"
#include "config_store.h"
#include "connectivity_provider.h"
#include "data_repository.h"
#include "device_listener.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "firebase_auth.h"
#include "firebase_paths.h"
#include "firebase_rtdb.h"
#include "flash_fs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "reading_store.h"
#include "reading_uploader.h"
#include "sensor_provider.h"
#include "unity.h"
#include "utils.h"

/*
 * Board-only: needs the Wi-Fi and Firebase credentials of the product sdkconfig.
 * pytest-embedded runs each case after a reset (docs/adr/0006-pytest-embedded-integracao.md), so
 * every case starts from a clean boot and brings up what it needs (ensure_online()). The cleanup
 * case is the last one in this file and wipes what the others left in the Firebase and in flash.
 */

#define EPOCH_2025_01_01_S 1735689600LL
#define MIN_ID_TOKEN_LEN   100U
#define STREAM_WAIT_MS     20000U
#define STREAM_POLL_MS     100U
#define ONLINE_WAIT_MS     30000U
/* Reconnecting after a deliberate disconnect: the backoff starts at 1 s, then join and DHCP. */
#define RECONNECT_WAIT_MS   60000U
#define DROP_WAIT_MS        5000U
#define SEED_READINGS       3U
#define SEED_FIRST_MS       1000
#define SEED_SENSORS_MASK   0x05U
#define SEED_ACTUATORS_MASK 0x03U
#define WIPE_MAX_FILES      64U
/* How long each output stays on, so it can be seen on the board. */
#define OUTPUT_VISIBLE_MS   1000U
#define TEMPERATURE_ENABLED FIREBASE_SENSORS_PATH "/temperature_1/enabled"
/* Must match test/integration/sdkconfig.defaults: the cleanup deletes this device's nodes. */
#define INTEGRATION_DEVICE_ID "integration-test"
/* In the Realtime Database, writing null deletes the node. */
#define DELETE_NODE_BODY    "null"
#define DATABASE_ERROR_HINT "see the errors above (sign-in, CONFIG_FIREBASE_DB_URL, database rules)"

static const char *TAG = "test_integration";

static char s_token[FIREBASE_ID_TOKEN_MAX_LEN];
static char s_sensors_json[SENSOR_CATALOG_JSON_MAX_LEN];
static char s_actuators_json[ACTUATOR_CATALOG_JSON_MAX_LEN];
static bool s_online;

void setUp(void)
{
}

/* A failed assertion jumps out of a test before its own cleanup: without this, the next test that
 * initializes the sensors would hit the "already initialized" assert. Safe when not initialized. */
void tearDown(void)
{
    sensor_provider_deinit();
}

/* connectivity_provider_init() returns at once: polls until the IP and the SNTP time are there. */
static bool wait_connected_with_time(void)
{
    for (uint32_t waited_ms = 0U; waited_ms < ONLINE_WAIT_MS; waited_ms += STREAM_POLL_MS) {
        if (connectivity_provider_is_connected() && connectivity_provider_is_time_valid()) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(STREAM_POLL_MS));
    }
    return false;
}

/* Reading with the current time and nothing else, as the uploader would send it. */
static esp_err_t push_test_reading(const char *sensors_json)
{
    const data_repository_reading_t reading = {
        .timestamp_ms = utils_epoch_ms(), .sensors_json = sensors_json, .actuators_json = "{}"};
    return data_repository_push_readings(&reading, 1U);
}

static void ensure_online(void)
{
    if (s_online) {
        return;
    }
    TEST_ASSERT_EQUAL_INT32(ESP_OK, connectivity_provider_init());
    TEST_ASSERT_TRUE_MESSAGE(wait_connected_with_time(),
                             "Check CONFIG_WIFI_SSID/PASSWORD (2.4 GHz network)");
    TEST_ASSERT_EQUAL_INT32(ESP_OK, firebase_rtdb_init());
    s_online = true;
}

/* Polls until the stream has applied `expected` to the temperature sensor. */
static bool wait_temperature_enabled(bool expected)
{
    for (uint32_t waited_ms = 0U; waited_ms < STREAM_WAIT_MS; waited_ms += STREAM_POLL_MS) {
        if (sensor_provider_is_enabled(SENSOR_TEMPERATURE_1) == expected) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(STREAM_POLL_MS));
    }
    return false;
}

TEST_CASE("test_integration_wifi_connects_and_clock_syncs", "[integration]")
{
    ensure_online();

    TEST_ASSERT_GREATER_THAN_INT64(EPOCH_2025_01_01_S, (int64_t)time(NULL));
}

TEST_CASE("test_integration_firebase_auth_signs_in", "[integration]")
{
    ensure_online();
    TEST_ASSERT_TRUE_MESSAGE(firebase_auth_is_enabled(), "Set CONFIG_FIREBASE_USER_EMAIL");

    TEST_ASSERT_EQUAL_INT32_MESSAGE(
        ESP_OK, firebase_auth_get_token(s_token, sizeof(s_token)),
        "Sign-in failed: see the firebase_auth/http_client errors above (TLS, API key, password)");
    TEST_ASSERT_GREATER_THAN_size_t(MIN_ID_TOKEN_LEN, strlen(s_token));
}

TEST_CASE("test_integration_reading_is_written_to_database", "[integration]")
{
    ensure_online();

    TEST_ASSERT_EQUAL_INT32_MESSAGE(ESP_OK, push_test_reading("{\"integration_test\":true}"),
                                    "PATCH failed: " DATABASE_ERROR_HINT);
}

TEST_CASE("test_integration_catalog_is_patched_into_database", "[integration]")
{
    ensure_online();
    TEST_ASSERT_EQUAL_INT32(
        ESP_OK, sensor_provider_catalog_to_json(s_sensors_json, sizeof(s_sensors_json)));
    TEST_ASSERT_EQUAL_INT32(
        ESP_OK, actuator_controller_catalog_to_json(s_actuators_json, sizeof(s_actuators_json)));

    TEST_ASSERT_EQUAL_INT32_MESSAGE(
        ESP_OK, data_repository_publish_catalog(s_sensors_json, s_actuators_json),
        "PATCH failed: " DATABASE_ERROR_HINT);
}

/* Not checked automatically: watch each simple output (the lights, then the relays) switch on for
 * 1 s. The curtain has its own test. */
TEST_CASE("test_integration_outputs_switch_on_and_off", "[integration]")
{
    TEST_ASSERT_EQUAL_INT32_MESSAGE(ESP_OK, actuator_controller_init(),
                                    "Check CONFIG_RGB_LED_GPIO and CONFIG_ACTUATOR_*_GPIO");
    for (uint32_t i = 0U; i < (uint32_t)ACTUATOR_COUNT; i++) {
        const actuator_t actuator = (actuator_t)i;
        if (actuator_controller_info(actuator)->kind == ACTUATOR_KIND_CURTAIN) {
            continue;
        }
        actuator_controller_set_enabled(actuator, true);
        actuator_controller_set_command(actuator, true);
        TEST_ASSERT_TRUE_MESSAGE(actuator_controller_is_on(actuator), "Output driver failed");
        vTaskDelay(pdMS_TO_TICKS(OUTPUT_VISIBLE_MS));
        actuator_controller_set_command(actuator, false);
        TEST_ASSERT_FALSE(actuator_controller_is_on(actuator));
    }
    actuator_controller_deinit();
}

/* Not checked automatically: listen to the curtain relays and watch the curtain: up for 1 s, stop,
 * down for 1 s, stop. Going from up to down must pause (CONFIG_ACTUATOR_CURTAIN_REVERSE_DELAY_MS)
 * with both relays off: no click of both at once. */
TEST_CASE("test_integration_curtain_moves_both_directions_and_stops", "[integration]")
{
    static const char *const commands[] = {"up", "stop", "down", "stop"};
    static const actuator_curtain_t expected[] = {ACTUATOR_CURTAIN_UP, ACTUATOR_CURTAIN_STOP,
                                                  ACTUATOR_CURTAIN_DOWN, ACTUATOR_CURTAIN_STOP};
    TEST_ASSERT_EQUAL_INT32_MESSAGE(ESP_OK, actuator_controller_init(),
                                    "Check CONFIG_ACTUATOR_CURTAIN_UP_GPIO / _DOWN_GPIO");
    actuator_controller_set_enabled(ACTUATOR_CURTAIN_1, true);

    for (size_t i = 0U; i < (sizeof(commands) / sizeof(commands[0])); i++) {
        actuator_controller_set_curtain(ACTUATOR_CURTAIN_1, commands[i]);
        TEST_ASSERT_EQUAL_INT32_MESSAGE(
            expected[i], actuator_controller_get_curtain(ACTUATOR_CURTAIN_1), commands[i]);
        vTaskDelay(pdMS_TO_TICKS(OUTPUT_VISIBLE_MS));
    }
    actuator_controller_deinit();
}

/* Not checked automatically: watch the RGB LED show red, green and blue for 1 s each. QEMU has no
 * RMT, so this is the only test of the real rgb_led HAL. */
TEST_CASE("test_integration_rgb_light_shows_colors", "[integration]")
{
    static const char *const colors[] = {"#ff0000", "#00ff00", "#0000ff"};
    static const uint32_t expected[] = {0xFF0000U, 0x00FF00U, 0x0000FFU};
    TEST_ASSERT_EQUAL_INT32(ESP_OK, actuator_controller_init());
    actuator_controller_set_enabled(ACTUATOR_RGB_LIGHT_1, true);
    actuator_controller_set_command(ACTUATOR_RGB_LIGHT_1, true);

    for (size_t i = 0U; i < (sizeof(colors) / sizeof(colors[0])); i++) {
        actuator_controller_set_color(ACTUATOR_RGB_LIGHT_1, colors[i]);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(expected[i],
                                        actuator_controller_get_color(ACTUATOR_RGB_LIGHT_1),
                                        "RGB LED driver failed");
        vTaskDelay(pdMS_TO_TICKS(OUTPUT_VISIBLE_MS));
    }
    actuator_controller_deinit();
}

TEST_CASE("test_integration_listener_applies_snapshot_and_live_change", "[integration]")
{
    ensure_online();
    /* The snapshot also switches the actuators (all off here): their outputs must exist. */
    TEST_ASSERT_EQUAL_INT32(ESP_OK, actuator_controller_init());
    TEST_ASSERT_EQUAL_INT32_MESSAGE(ESP_OK, firebase_rtdb_put(TEMPERATURE_ENABLED, "true"),
                                    "PUT failed: " DATABASE_ERROR_HINT);
    TEST_ASSERT_EQUAL_INT32(ESP_OK, device_listener_init());

    /* The first event of every stream is the whole node. */
    TEST_ASSERT_TRUE_MESSAGE(wait_temperature_enabled(true), "Snapshot not applied within 20 s");
    /* A change while the stream is open arrives as another event. */
    TEST_ASSERT_EQUAL_INT32(ESP_OK, firebase_rtdb_put(TEMPERATURE_ENABLED, "false"));
    TEST_ASSERT_TRUE_MESSAGE(wait_temperature_enabled(false),
                             "Live change not applied within 20 s");

    /* Cancels the real TLS stream: must return within ~1 s. */
    TEST_ASSERT_EQUAL_INT32(ESP_OK, device_listener_deinit());
    actuator_controller_deinit();
}

TEST_CASE("test_integration_stack_deinit_then_init_reconnects", "[integration]")
{
    ensure_online();
    firebase_rtdb_deinit();
    connectivity_provider_deinit();
    s_online = false;

    ensure_online();
    TEST_ASSERT_EQUAL_INT32(ESP_OK, push_test_reading("{\"integration_test\":1}"));
}

/* Real LittleFS on the "storage" partition. Uses its own file, so the readings and the config kept
 * by the device are untouched. */
#define FS_TEST_FILE "itest.bin"

static void count_test_file(const char *name, void *ctx)
{
    uint32_t *found = (uint32_t *)ctx;
    if (strcmp(name, FS_TEST_FILE) == 0) {
        (*found)++;
    }
}

TEST_CASE("test_integration_flash_fs_appends_reads_replaces_and_removes", "[integration]")
{
    const uint8_t first[] = {1U, 2U, 3U};
    const uint8_t second[] = {4U, 5U};
    const uint8_t replacement[] = {9U};
    uint8_t bytes[8] = {0};
    flash_fs_buffer_t buffer = {.data = bytes, .capacity = sizeof(bytes), .length = 0U};
    size_t size = 0U;
    size_t free_before = 0U;
    size_t free_after = 0U;
    uint32_t found = 0U;

    TEST_ASSERT_EQUAL_INT32_MESSAGE(ESP_OK, flash_fs_mount(),
                                    "Is the 'storage' partition in the flashed table?");
    (void)flash_fs_remove(FS_TEST_FILE); /* Left by an earlier failed run: NOT_FOUND is fine. */
    TEST_ASSERT_EQUAL_INT32(ESP_OK, flash_fs_free_bytes(&free_before));

    TEST_ASSERT_EQUAL_INT32(ESP_OK, flash_fs_append(FS_TEST_FILE, first, sizeof(first)));
    TEST_ASSERT_EQUAL_INT32(ESP_OK, flash_fs_append(FS_TEST_FILE, second, sizeof(second)));
    TEST_ASSERT_EQUAL_INT32(ESP_OK, flash_fs_size(FS_TEST_FILE, &size));
    TEST_ASSERT_EQUAL_UINT32(5U, (uint32_t)size);
    TEST_ASSERT_EQUAL_INT32(ESP_OK, flash_fs_read(FS_TEST_FILE, 2U, &buffer));
    TEST_ASSERT_EQUAL_UINT32(3U, (uint32_t)buffer.length);
    TEST_ASSERT_EQUAL_UINT8(3U, bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(5U, bytes[2]);
    TEST_ASSERT_EQUAL_INT32(ESP_OK, flash_fs_list(count_test_file, &found));
    TEST_ASSERT_EQUAL_UINT32(1U, found);

    TEST_ASSERT_EQUAL_INT32(ESP_OK,
                            flash_fs_replace(FS_TEST_FILE, replacement, sizeof(replacement)));
    TEST_ASSERT_EQUAL_INT32(ESP_OK, flash_fs_size(FS_TEST_FILE, &size));
    TEST_ASSERT_EQUAL_UINT32(1U, (uint32_t)size);

    TEST_ASSERT_EQUAL_INT32(ESP_OK, flash_fs_remove(FS_TEST_FILE));
    TEST_ASSERT_EQUAL_INT32(ESP_ERR_NOT_FOUND, flash_fs_size(FS_TEST_FILE, &size));
    TEST_ASSERT_EQUAL_INT32(ESP_OK, flash_fs_free_bytes(&free_after));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)free_before, (uint32_t)free_after);
    flash_fs_unmount();
}

/* The internal sensor measures the die, not the air: it follows the load (Wi-Fi and TLS in the
 * cases before this one, with no time to cool between resets), so the bound is wide: 20 to 50 degC.
 */
TEST_CASE("test_integration_chip_temperature_reads_plausible_value", "[integration]")
{
    sensor_readings_t readings = {0};
    TEST_ASSERT_EQUAL_INT32(ESP_OK, sensor_provider_init());
    sensor_provider_set_enabled(SENSOR_TEMPERATURE_1, true);

    sensor_provider_read(&readings);
    ESP_LOGI(TAG, "Chip temperature: %.1f degC", (double)readings.value[SENSOR_TEMPERATURE_1]);
    TEST_ASSERT_TRUE_MESSAGE(isfinite(readings.value[SENSOR_TEMPERATURE_1]),
                             "Read failed: see the warning above (outside the driver's range?)");
    TEST_ASSERT_FLOAT_WITHIN(15.0f, 35.0f, readings.value[SENSOR_TEMPERATURE_1]);
    sensor_provider_deinit();
}

/* Opens and closes `gpio` on its own, before sensor_provider claims it. */
static bool adc_is_calibrated(int32_t gpio)
{
    analog_input_handle_t input = NULL;
    TEST_ASSERT_EQUAL_INT32_MESSAGE(ESP_OK, analog_input_init(gpio, &input),
                                    "GPIO is not an ADC1 pin");
    const bool calibrated = analog_input_is_calibrated(input);
    analog_input_deinit(input);
    return calibrated;
}

/* LM35 on the ADC: air temperature near a room's (the chip reads higher). Warm it with a finger
 * between runs to see the value follow. "[ignore]": pytest-embedded skips it (it read 50.8 degC on
 * 2026-11-16 and the wiring is still to be checked); drop the tag to bring it back. */
TEST_CASE("test_integration_lm35_reads_plausible_air_temperature", "[integration][ignore]")
{
    sensor_readings_t readings = {0};
    TEST_ASSERT_TRUE_MESSAGE(adc_is_calibrated(CONFIG_LM35_GPIO), "ADC calibration eFuses missing");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(ESP_OK, sensor_provider_init(),
                                    "Check CONFIG_LM35_GPIO and CONFIG_LDR_GPIO (ADC1 pins)");
    sensor_provider_set_enabled(SENSOR_TEMPERATURE_2, true);

    sensor_provider_read(&readings);
    ESP_LOGI(TAG, "LM35 temperature: %.1f degC", (double)readings.value[SENSOR_TEMPERATURE_2]);
    TEST_ASSERT_TRUE_MESSAGE(isfinite(readings.value[SENSOR_TEMPERATURE_2]),
                             "Read failed: LM35 not powered from 5 V or wired to the wrong pin?");
    TEST_ASSERT_FLOAT_WITHIN(20.0f, 25.0f, readings.value[SENSOR_TEMPERATURE_2]);
    sensor_provider_deinit();
}

/* MQ-135 on the ADC: needs the sensor preheated (24 h for a stable reading). Only checks that the
 * wiring gives a finite, plausible value; calibrate CONFIG_MQ135_R0_OHMS in clean air. */
TEST_CASE("test_integration_mq135_reads_plausible_gas_level", "[integration]")
{
    sensor_readings_t readings = {0};
    TEST_ASSERT_TRUE_MESSAGE(adc_is_calibrated(CONFIG_MQ135_GPIO),
                             "ADC calibration eFuses missing");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(ESP_OK, sensor_provider_init(),
                                    "Check CONFIG_MQ135_GPIO (ADC1 pin)");
    sensor_provider_set_enabled(SENSOR_GAS, true);

    sensor_provider_read(&readings);
    ESP_LOGI(TAG, "Gas: %.0f ppm (CO2 equivalent)", (double)readings.value[SENSOR_GAS]);
    TEST_ASSERT_TRUE_MESSAGE(isfinite(readings.value[SENSOR_GAS]),
                             "Read failed: divider on AO, 5 V supply and GPIO right?");
    TEST_ASSERT_TRUE(readings.value[SENSOR_GAS] > 0.0f);
    sensor_provider_deinit();
}

/* LDR on the ADC: calibrated on a real module, and lux within range. Cover and light the LDR
 * between runs to see the value follow. */
TEST_CASE("test_integration_ldr_reads_calibrated_luminosity", "[integration]")
{
    sensor_readings_t readings = {0};
    TEST_ASSERT_TRUE_MESSAGE(adc_is_calibrated(CONFIG_LDR_GPIO), "ADC calibration eFuses missing");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(ESP_OK, sensor_provider_init(),
                                    "Check CONFIG_LDR_GPIO and CONFIG_LM35_GPIO (ADC1 pins)");
    sensor_provider_set_enabled(SENSOR_LUMINOSITY_1, true);

    sensor_provider_read(&readings);
    ESP_LOGI(TAG, "Luminosity: %.0f lux", (double)readings.value[SENSOR_LUMINOSITY_1]);
    TEST_ASSERT_TRUE_MESSAGE(isfinite(readings.value[SENSOR_LUMINOSITY_1]),
                             "Read failed: see the warning above");
    TEST_ASSERT_TRUE(readings.value[SENSOR_LUMINOSITY_1] >= 0.0f);
    sensor_provider_deinit();
}

/* ---- Flash storage (ADR 0004) ---- */

typedef struct {
    char names[WIPE_MAX_FILES][FLASH_FS_NAME_MAX_LEN];
    uint32_t count;
} file_list_t;

static file_list_t s_files; /* Only the main task uses it (wipe_storage). */

static void collect_name(const char *name, void *ctx)
{
    file_list_t *list = (file_list_t *)ctx;
    if (list->count < WIPE_MAX_FILES) {
        (void)strlcpy(list->names[list->count], name, sizeof(list->names[0]));
        list->count++;
    }
}

/* Removes every file of the storage partition: readings, config and test leftovers. */
static void wipe_storage(void)
{
    s_files.count = 0U;
    TEST_ASSERT_EQUAL_INT32(ESP_OK, flash_fs_list(collect_name, &s_files));
    for (uint32_t i = 0U; i < s_files.count; i++) {
        TEST_ASSERT_EQUAL_INT32(ESP_OK, flash_fs_remove(s_files.names[i]));
    }
}

static void mount_clean_storage(void)
{
    TEST_ASSERT_EQUAL_INT32_MESSAGE(ESP_OK, flash_fs_mount(),
                                    "Is the 'storage' partition in the flashed table?");
    wipe_storage();
}

/* Record n of the seeds: timestamp SEED_FIRST_MS + n, first sensor n, the others without value. */
static reading_record_t seed_record(uint32_t n)
{
    reading_record_t record = {0};
    record.timestamp_ms = SEED_FIRST_MS + (int64_t)n;
    for (uint32_t i = 0U; i < (uint32_t)SENSOR_COUNT; i++) {
        record.sensors.value[i] = NAN;
    }
    record.sensors.value[0] = (float)n;
    return record;
}

static size_t pending_readings(void)
{
    reading_record_t records[DATA_REPOSITORY_BATCH_MAX];
    size_t count = 0U;
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_store_peek(records, DATA_REPOSITORY_BATCH_MAX, &count));
    return count;
}

/* Polls until the uploader has sent everything (a reading only leaves the store after a 2xx). */
static bool wait_for_empty_store(uint32_t timeout_ms)
{
    for (uint32_t waited_ms = 0U; waited_ms < timeout_ms; waited_ms += STREAM_POLL_MS) {
        if (pending_readings() == 0U) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(STREAM_POLL_MS));
    }
    return false;
}

static bool wait_for_connection_state(bool connected, uint32_t timeout_ms)
{
    for (uint32_t waited_ms = 0U; waited_ms < timeout_ms; waited_ms += STREAM_POLL_MS) {
        if (connectivity_provider_is_connected() == connected) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(STREAM_POLL_MS));
    }
    return false;
}

/* Stage 1: leaves the config and some readings in flash, then resets the board. */
static void persistence_write_then_reset(void)
{
    mount_clean_storage();
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_store_init());
    for (uint32_t n = 0U; n < SEED_READINGS; n++) {
        const reading_record_t record = seed_record(n);
        TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_store_append(&record));
    }
    const config_store_enabled_t enabled = {.sensors = SEED_SENSORS_MASK,
                                            .actuators = SEED_ACTUATORS_MASK};
    TEST_ASSERT_EQUAL_INT32(ESP_OK, config_store_save(&enabled));
    esp_restart(); /* Not reached when an assertion above fails: the menu comes back instead. */
}

/* Stage 2: after the reset, what stage 1 wrote is still there, in order. */
static void persistence_verify_after_reset(void)
{
    reading_record_t records[DATA_REPOSITORY_BATCH_MAX];
    size_t count = 0U;
    config_store_enabled_t enabled = {0};

    TEST_ASSERT_EQUAL_INT32(ESP_OK, flash_fs_mount());
    TEST_ASSERT_EQUAL_INT32_MESSAGE(ESP_OK, config_store_load(&enabled), "config.bin lost");
    TEST_ASSERT_EQUAL_HEX32(SEED_SENSORS_MASK, enabled.sensors);
    TEST_ASSERT_EQUAL_HEX32(SEED_ACTUATORS_MASK, enabled.actuators);

    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_store_init());
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_store_peek(records, DATA_REPOSITORY_BATCH_MAX, &count));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(SEED_READINGS, (uint32_t)count, "Readings lost in the reset");
    for (uint32_t n = 0U; n < SEED_READINGS; n++) {
        TEST_ASSERT_EQUAL_INT64(SEED_FIRST_MS + (int64_t)n, records[n].timestamp_ms);
        TEST_ASSERT_EQUAL_FLOAT((float)n, records[n].sensors.value[0]);
    }
    reading_store_deinit();
    wipe_storage();
    flash_fs_unmount();
}

TEST_CASE_MULTIPLE_STAGES("test_integration_reading_store_and_config_survive_a_reset",
                          "[integration]", persistence_write_then_reset,
                          persistence_verify_after_reset)

/* The Wi-Fi is cut on purpose (esp_wifi_disconnect): readings sampled meanwhile stay in flash, and
 * the uploader sends them once the connectivity_provider has reconnected. */
TEST_CASE("test_integration_uploader_sends_stored_readings_after_wifi_drop", "[integration]")
{
    ensure_online();
    mount_clean_storage();
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_store_init());
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_uploader_init(connectivity_provider_is_connected));
    TEST_ASSERT_TRUE_MESSAGE(wait_for_empty_store(STREAM_WAIT_MS), "Nothing to send yet, but busy");

    TEST_ASSERT_EQUAL_INT32(ESP_OK, esp_wifi_disconnect());
    TEST_ASSERT_TRUE_MESSAGE(wait_for_connection_state(false, DROP_WAIT_MS), "Wi-Fi did not drop");
    for (uint32_t n = 0U; n < SEED_READINGS; n++) {
        reading_record_t record = seed_record(n);
        record.timestamp_ms = utils_epoch_ms();
        TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_store_append(&record));
    }
    if (!connectivity_provider_is_connected()) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(SEED_READINGS, (uint32_t)pending_readings(),
                                         "Readings must wait in flash while offline");
    }

    TEST_ASSERT_TRUE_MESSAGE(wait_for_connection_state(true, RECONNECT_WAIT_MS),
                             "Wi-Fi did not reconnect");
    TEST_ASSERT_TRUE_MESSAGE(wait_for_empty_store(RECONNECT_WAIT_MS),
                             "Stored readings not sent after reconnecting: " DATABASE_ERROR_HINT);
    TEST_ASSERT_EQUAL_INT32(ESP_OK, reading_uploader_deinit());
    reading_store_deinit();
    wipe_storage();
    flash_fs_unmount();
}

/* Runs last (the menu lists the cases in declaration order): removes everything the others wrote,
 * in the Firebase and in the flash storage. */
TEST_CASE("test_integration_cleanup_deletes_test_nodes", "[integration]")
{
    /* Never delete a real device's data. */
    TEST_ASSERT_EQUAL_STRING_MESSAGE(INTEGRATION_DEVICE_ID, CONFIG_DEVICE_ID,
                                     "Refusing to delete: not the integration-test device");
    ensure_online();

    TEST_ASSERT_EQUAL_INT32_MESSAGE(ESP_OK,
                                    firebase_rtdb_put(FIREBASE_READINGS_PATH, DELETE_NODE_BODY),
                                    "Delete failed: " DATABASE_ERROR_HINT);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(ESP_OK,
                                    firebase_rtdb_put(FIREBASE_DEVICE_PATH, DELETE_NODE_BODY),
                                    "Delete failed: " DATABASE_ERROR_HINT);
    mount_clean_storage();
    flash_fs_unmount();
}
