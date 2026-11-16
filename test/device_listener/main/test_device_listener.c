#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "actuator_controller.h"
#include "device_listener.h"
#include "fake_http_client.h"
#include "fake_rgb_led.h"
#include "firebase_rtdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensor_provider.h"
#include "unity.h"

/* Real device_listener + providers + firebase_rtdb + firebase_auth (auth disabled) over the fake
 * HAL. */

#define STREAM_URL    "https://test-db.example.com/devices/test-device.json"
#define LONG_PATH_LEN 80U
/* First reconnect waits 1 s (DEVICE_LISTENER_RETRY_INITIAL_MS); give it some margin. */
#define FIRST_RETRY_WAIT_MS 1300U
/* Lets the listener task reach the stream before the check. */
#define TASK_START_WAIT_MS 50U

/* Fits the snapshot of the whole device node built by build_full_node_event(). */
#define FULL_NODE_EVENT_LEN 2048U

#define SNAPSHOT_ALL_ON                                                                            \
    "{\"path\":\"/\",\"data\":{"                                                                   \
    "\"sensors\":{\"temperature_1\":{\"type\":\"temperature\",\"enabled\":true},"                  \
    "\"luminosity_2\":{\"enabled\":true}},"                                                        \
    "\"actuators\":{\"rgb_light_1\":{\"enabled\":true,\"command\":true},"                          \
    "\"light_1\":{\"enabled\":true,\"command\":false}}}}"

static char s_long_path_event[LONG_PATH_LEN + 48U];
static char s_full_node_event[FULL_NODE_EVENT_LEN];

static void emit(const fake_sse_event_t *events, size_t count)
{
    TEST_ASSERT_TRUE_MESSAGE(fake_http_client_emit(events, count),
                             "The listener task did not consume the events");
}

#define EMIT(...)                                                                                  \
    do {                                                                                           \
        const fake_sse_event_t events_[] = {__VA_ARGS__};                                          \
        emit(events_, sizeof(events_) / sizeof(events_[0]));                                       \
    } while (0)

static void disable_sensors(void)
{
    for (uint32_t i = 0U; i < (uint32_t)SENSOR_COUNT; i++) {
        sensor_provider_set_enabled((sensor_t)i, false);
    }
}

static void start_listener(bool stream_blocks)
{
    fake_http_client.stream_blocks = stream_blocks;
    TEST_ASSERT_EQUAL_INT32(ESP_OK, device_listener_init());
}

void setUp(void)
{
    fake_http_client_reset();
    fake_rgb_led_reset();
    disable_sensors();
    TEST_ASSERT_EQUAL_INT32(ESP_OK, actuator_controller_init()); /* Every actuator off. */
    TEST_ASSERT_EQUAL_INT32(ESP_OK, firebase_rtdb_init());
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, device_listener_deinit());
    firebase_rtdb_deinit();
    actuator_controller_deinit();
}

/* ---- Stream lifecycle ---- */

TEST_CASE("test_device_listener_init_opens_one_stream_on_device_node", "[device_listener]")
{
    start_listener(true);
    vTaskDelay(pdMS_TO_TICKS(TASK_START_WAIT_MS));

    TEST_ASSERT_EQUAL_UINT32(1U, fake_http_client.stream_calls);
    TEST_ASSERT_EQUAL_STRING(STREAM_URL, fake_http_client.last_url);
}

TEST_CASE("test_device_listener_failed_stream_reconnects_after_backoff", "[device_listener]")
{
    fake_http_client.stream_result = ESP_FAIL;
    start_listener(false);
    vTaskDelay(pdMS_TO_TICKS(TASK_START_WAIT_MS));
    TEST_ASSERT_EQUAL_UINT32(1U, fake_http_client.stream_calls);

    vTaskDelay(pdMS_TO_TICKS(FIRST_RETRY_WAIT_MS));

    TEST_ASSERT_EQUAL_UINT32(2U, fake_http_client.stream_calls);
}

TEST_CASE("test_device_listener_deinit_during_backoff_returns_ok", "[device_listener]")
{
    fake_http_client.stream_result = ESP_FAIL;
    start_listener(false);
    vTaskDelay(pdMS_TO_TICKS(TASK_START_WAIT_MS));

    TEST_ASSERT_EQUAL_INT32(ESP_OK, device_listener_deinit());
    TEST_ASSERT_EQUAL_UINT32(1U, fake_http_client.stream_calls);
}

TEST_CASE("test_device_listener_deinit_cancels_open_stream_and_reinit_works", "[device_listener]")
{
    start_listener(true);
    EMIT({"put", "{\"path\":\"/sensors/luminosity_2/enabled\",\"data\":true}"});

    TEST_ASSERT_EQUAL_INT32(ESP_OK, device_listener_deinit());
    TEST_ASSERT_EQUAL_UINT32(1U, fake_http_client.cancel_calls);

    start_listener(true);
    EMIT({"put", "{\"path\":\"/sensors/gas_1/enabled\",\"data\":true}"});
    TEST_ASSERT_TRUE(sensor_provider_is_enabled(SENSOR_LUMINOSITY_2));
    TEST_ASSERT_TRUE(sensor_provider_is_enabled(SENSOR_GAS));
}

TEST_CASE("test_device_listener_deinit_when_not_initialized_returns_ok", "[device_listener]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, device_listener_deinit());
}

/* ---- put ---- */

TEST_CASE("test_device_listener_snapshot_applies_every_field", "[device_listener]")
{
    start_listener(true);
    EMIT({"put", SNAPSHOT_ALL_ON});

    TEST_ASSERT_TRUE(sensor_provider_is_enabled(SENSOR_TEMPERATURE_1));
    TEST_ASSERT_TRUE(sensor_provider_is_enabled(SENSOR_LUMINOSITY_2));
    TEST_ASSERT_FALSE(sensor_provider_is_enabled(SENSOR_GAS)); /* Missing: disabled. */
    TEST_ASSERT_TRUE(actuator_controller_is_on(ACTUATOR_RGB_LIGHT_1));
    TEST_ASSERT_TRUE(actuator_controller_is_enabled(ACTUATOR_LIGHT_1));
    TEST_ASSERT_FALSE(actuator_controller_is_on(ACTUATOR_LIGHT_1));
}

/* Appends to s_full_node_event at `*len`. */
static void append_event(size_t *len, const char *format, ...)
    __attribute__((format(printf, 2, 3))); /* GNU extension: checks the arguments (-Wformat). */

static void append_event(size_t *len, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int32_t written = (int32_t)vsnprintf(&s_full_node_event[*len],
                                               sizeof(s_full_node_event) - *len, format, args);
    va_end(args);
    TEST_ASSERT_TRUE(written > 0);
    *len += (size_t)written;
    TEST_ASSERT_TRUE(*len < sizeof(s_full_node_event));
}

/* The first put of a stream carries the whole node: every catalog field of every sensor and
 * actuator, plus the SPA fields. */
static void build_full_node_event(void)
{
    size_t len = 0U;
    append_event(&len, "{\"path\":\"/\",\"data\":{\"sensors\":{");
    for (uint32_t i = 0U; i < (uint32_t)SENSOR_COUNT; i++) {
        append_event(&len,
                     "%s\"%s\":{\"type\":\"t\",\"unit\":\"u\",\"simulated\":true,\"enabled\":true}",
                     (i == 0U) ? "" : ",", sensor_provider_info((sensor_t)i)->id);
    }
    append_event(&len, "},\"actuators\":{");
    for (uint32_t i = 0U; i < (uint32_t)ACTUATOR_COUNT; i++) {
        const actuator_info_t *info = actuator_controller_info((actuator_t)i);
        append_event(&len,
                     "%s\"%s\":{\"type\":\"t\",\"simulated\":true,\"enabled\":true,"
                     "\"command\":%s,\"color\":\"#102030\"}",
                     (i == 0U) ? "" : ",", info->id,
                     (info->kind == ACTUATOR_KIND_CURTAIN) ? "\"up\"" : "true");
    }
    append_event(&len, "}}}");
}

TEST_CASE("test_device_listener_full_device_node_fits_the_json_token_limit", "[device_listener]")
{
    build_full_node_event();
    start_listener(true);
    EMIT({"put", s_full_node_event});

    for (uint32_t i = 0U; i < (uint32_t)SENSOR_COUNT; i++) {
        TEST_ASSERT_TRUE(sensor_provider_is_enabled((sensor_t)i));
    }
    for (uint32_t i = 0U; i < (uint32_t)ACTUATOR_COUNT; i++) {
        TEST_ASSERT_TRUE(actuator_controller_is_on((actuator_t)i));
    }
}

TEST_CASE("test_device_listener_curtain_command_text_moves_the_curtain", "[device_listener]")
{
    start_listener(true);
    EMIT({"put",
          "{\"path\":\"/actuators/curtain_1\",\"data\":{\"enabled\":true,\"command\":\"up\"}}"});
    TEST_ASSERT_EQUAL_INT32(ACTUATOR_CURTAIN_UP,
                            actuator_controller_get_curtain(ACTUATOR_CURTAIN_1));

    EMIT({"put", "{\"path\":\"/actuators/curtain_1/command\",\"data\":\"down\"}"});
    TEST_ASSERT_EQUAL_INT32(ACTUATOR_CURTAIN_DOWN,
                            actuator_controller_get_curtain(ACTUATOR_CURTAIN_1));

    EMIT({"put", "{\"path\":\"/actuators/curtain_1/command\",\"data\":\"stop\"}"});
    TEST_ASSERT_EQUAL_INT32(ACTUATOR_CURTAIN_STOP,
                            actuator_controller_get_curtain(ACTUATOR_CURTAIN_1));
}

TEST_CASE("test_device_listener_curtain_patch_sets_direction", "[device_listener]")
{
    start_listener(true);
    EMIT({"put", "{\"path\":\"/actuators/curtain_1/enabled\",\"data\":true}"},
         {"patch", "{\"path\":\"/actuators/curtain_1\",\"data\":{\"command\":\"up\"}}"});

    TEST_ASSERT_EQUAL_INT32(ACTUATOR_CURTAIN_UP,
                            actuator_controller_get_curtain(ACTUATOR_CURTAIN_1));
}

TEST_CASE("test_device_listener_curtain_deleted_or_boolean_command_stops", "[device_listener]")
{
    start_listener(true);
    EMIT({"put",
          "{\"path\":\"/actuators/curtain_1\",\"data\":{\"enabled\":true,\"command\":\"up\"}}"},
         {"patch", "{\"path\":\"/actuators/curtain_1\",\"data\":{\"command\":null}}"});
    TEST_ASSERT_EQUAL_INT32(ACTUATOR_CURTAIN_STOP,
                            actuator_controller_get_curtain(ACTUATOR_CURTAIN_1));

    EMIT({"put", "{\"path\":\"/actuators/curtain_1/command\",\"data\":\"down\"}"},
         {"put", "{\"path\":\"/actuators/curtain_1/command\",\"data\":true}"});
    TEST_ASSERT_EQUAL_INT32(ACTUATOR_CURTAIN_STOP,
                            actuator_controller_get_curtain(ACTUATOR_CURTAIN_1));
}

TEST_CASE("test_device_listener_relay_command_is_a_boolean", "[device_listener]")
{
    start_listener(true);
    EMIT({"put", "{\"path\":\"/actuators/relay_2\",\"data\":{\"enabled\":true,\"command\":true}}"});

    TEST_ASSERT_TRUE(actuator_controller_is_on(ACTUATOR_RELAY_2));
}

TEST_CASE("test_device_listener_rgb_light_command_reaches_the_led", "[device_listener]")
{
    start_listener(true);
    EMIT({"put", SNAPSHOT_ALL_ON});
    TEST_ASSERT_GREATER_THAN_UINT32(0U, fake_rgb_led.red);

    EMIT({"put", "{\"path\":\"/actuators/rgb_light_1/command\",\"data\":false}"});
    TEST_ASSERT_EQUAL_UINT32(0U, fake_rgb_led.red);
}

TEST_CASE("test_device_listener_snapshot_color_reaches_the_led", "[device_listener]")
{
    start_listener(true);
    EMIT({"put", "{\"path\":\"/\",\"data\":{\"actuators\":{\"rgb_light_1\":"
                 "{\"enabled\":true,\"command\":true,\"color\":\"#ff8000\"}}}}"});

    TEST_ASSERT_EQUAL_UINT32(0xFFU, fake_rgb_led.red);
    TEST_ASSERT_EQUAL_UINT32(0x80U, fake_rgb_led.green);
    TEST_ASSERT_EQUAL_UINT32(0x00U, fake_rgb_led.blue);
}

TEST_CASE("test_device_listener_color_change_while_on_updates_led", "[device_listener]")
{
    start_listener(true);
    EMIT({"put", SNAPSHOT_ALL_ON},
         {"put", "{\"path\":\"/actuators/rgb_light_1/color\",\"data\":\"#0000ff\"}"});

    TEST_ASSERT_EQUAL_HEX32(0x0000FFU, actuator_controller_get_color(ACTUATOR_RGB_LIGHT_1));
}

TEST_CASE("test_device_listener_patch_color_keeps_on_state", "[device_listener]")
{
    start_listener(true);
    EMIT({"put", SNAPSHOT_ALL_ON},
         {"patch", "{\"path\":\"/actuators/rgb_light_1\",\"data\":{\"color\":\"#00ff00\"}}"});

    TEST_ASSERT_TRUE(actuator_controller_is_on(ACTUATOR_RGB_LIGHT_1));
    TEST_ASSERT_EQUAL_HEX32(0x00FF00U, actuator_controller_get_color(ACTUATOR_RGB_LIGHT_1));
}

TEST_CASE("test_device_listener_deleted_or_non_text_color_uses_default", "[device_listener]")
{
    start_listener(true);
    EMIT({"put", SNAPSHOT_ALL_ON},
         {"put", "{\"path\":\"/actuators/rgb_light_1/color\",\"data\":\"#ff0000\"}"},
         {"put", "{\"path\":\"/actuators/rgb_light_1/color\",\"data\":null}"});
    TEST_ASSERT_EQUAL_HEX32(0xF0F0F0U, actuator_controller_get_color(ACTUATOR_RGB_LIGHT_1));

    EMIT({"put", "{\"path\":\"/actuators/rgb_light_1/color\",\"data\":\"#ff0000\"}"},
         {"put", "{\"path\":\"/actuators/rgb_light_1/color\",\"data\":16711680}"});
    TEST_ASSERT_EQUAL_HEX32(0xF0F0F0U, actuator_controller_get_color(ACTUATOR_RGB_LIGHT_1));
}

TEST_CASE("test_device_listener_too_long_color_uses_default", "[device_listener]")
{
    start_listener(true);
    EMIT({"put", SNAPSHOT_ALL_ON},
         {"put", "{\"path\":\"/actuators/rgb_light_1/color\",\"data\":\"#ff0000\"}"},
         {"put", "{\"path\":\"/actuators/rgb_light_1/color\",\"data\":\"#ff0000ff0000ff0000\"}"});

    TEST_ASSERT_EQUAL_HEX32(0xF0F0F0U, actuator_controller_get_color(ACTUATOR_RGB_LIGHT_1));
}

TEST_CASE("test_device_listener_color_on_plain_light_is_ignored", "[device_listener]")
{
    start_listener(true);
    EMIT({"put", SNAPSHOT_ALL_ON},
         {"put", "{\"path\":\"/actuators/light_1/color\",\"data\":\"#ff0000\"}"});

    TEST_ASSERT_TRUE(actuator_controller_is_enabled(ACTUATOR_LIGHT_1));
}

TEST_CASE("test_device_listener_null_snapshot_disables_everything", "[device_listener]")
{
    start_listener(true);
    EMIT({"put", SNAPSHOT_ALL_ON}, {"put", "{\"path\":\"/\",\"data\":null}"});

    TEST_ASSERT_FALSE(sensor_provider_is_enabled(SENSOR_TEMPERATURE_1));
    TEST_ASSERT_FALSE(actuator_controller_is_enabled(ACTUATOR_RGB_LIGHT_1));
    TEST_ASSERT_FALSE(actuator_controller_is_on(ACTUATOR_RGB_LIGHT_1));
}

TEST_CASE("test_device_listener_put_on_field_sets_it", "[device_listener]")
{
    start_listener(true);
    EMIT({"put", "{\"path\":\"/sensors/luminosity_1/enabled\",\"data\":true}"});

    TEST_ASSERT_TRUE(sensor_provider_is_enabled(SENSOR_LUMINOSITY_1));
    TEST_ASSERT_FALSE(sensor_provider_is_enabled(SENSOR_TEMPERATURE_1));
}

TEST_CASE("test_device_listener_put_on_item_replaces_its_fields", "[device_listener]")
{
    start_listener(true);
    EMIT({"put", SNAPSHOT_ALL_ON},
         {"put", "{\"path\":\"/actuators/rgb_light_1\",\"data\":{\"enabled\":true}}"});

    TEST_ASSERT_TRUE(actuator_controller_is_enabled(ACTUATOR_RGB_LIGHT_1));
    TEST_ASSERT_FALSE(actuator_controller_is_on(ACTUATOR_RGB_LIGHT_1)); /* command was replaced. */
    TEST_ASSERT_TRUE(actuator_controller_is_enabled(ACTUATOR_LIGHT_1)); /* Sibling untouched. */
}

TEST_CASE("test_device_listener_put_deleting_field_disables", "[device_listener]")
{
    start_listener(true);
    EMIT({"put", SNAPSHOT_ALL_ON},
         {"put", "{\"path\":\"/sensors/temperature_1/enabled\",\"data\":null}"});

    TEST_ASSERT_FALSE(sensor_provider_is_enabled(SENSOR_TEMPERATURE_1));
    TEST_ASSERT_TRUE(sensor_provider_is_enabled(SENSOR_LUMINOSITY_2));
}

TEST_CASE("test_device_listener_non_boolean_field_reads_as_false", "[device_listener]")
{
    start_listener(true);
    EMIT({"put", SNAPSHOT_ALL_ON},
         {"put", "{\"path\":\"/actuators/rgb_light_1/command\",\"data\":\"on\"}"});

    TEST_ASSERT_FALSE(actuator_controller_is_on(ACTUATOR_RGB_LIGHT_1));
}

TEST_CASE("test_device_listener_put_on_unrelated_path_changes_nothing", "[device_listener]")
{
    start_listener(true);
    EMIT({"put", SNAPSHOT_ALL_ON},
         {"put", "{\"path\":\"/sensors/temperature_1/type\",\"data\":\"x\"}"},
         {"put", "{\"path\":\"/sensors/temperature_10\",\"data\":null}"});

    TEST_ASSERT_TRUE(sensor_provider_is_enabled(SENSOR_TEMPERATURE_1));
}

/* ---- patch ---- */

TEST_CASE("test_device_listener_patch_on_item_keeps_other_fields", "[device_listener]")
{
    start_listener(true);
    EMIT({"put", SNAPSHOT_ALL_ON},
         {"patch", "{\"path\":\"/actuators/light_1\",\"data\":{\"command\":true}}"});

    TEST_ASSERT_TRUE(actuator_controller_is_on(ACTUATOR_LIGHT_1));
    TEST_ASSERT_TRUE(actuator_controller_is_on(ACTUATOR_RGB_LIGHT_1));
}

TEST_CASE("test_device_listener_patch_with_path_keys_sets_fields", "[device_listener]")
{
    start_listener(true);
    EMIT({"patch",
          "{\"path\":\"/\",\"data\":{\"actuators/rgb_light_1/enabled\":true,"
          "\"actuators/rgb_light_1/command\":true,\"sensors/gas_1\":{\"enabled\":true}}}"});

    TEST_ASSERT_TRUE(actuator_controller_is_on(ACTUATOR_RGB_LIGHT_1));
    TEST_ASSERT_TRUE(sensor_provider_is_enabled(SENSOR_GAS));
}

TEST_CASE("test_device_listener_patch_null_deletes_field", "[device_listener]")
{
    start_listener(true);
    EMIT({"put", SNAPSHOT_ALL_ON},
         {"patch", "{\"path\":\"/sensors\",\"data\":{\"luminosity_2\":null}}"});

    TEST_ASSERT_FALSE(sensor_provider_is_enabled(SENSOR_LUMINOSITY_2));
    TEST_ASSERT_TRUE(sensor_provider_is_enabled(SENSOR_TEMPERATURE_1));
}

TEST_CASE("test_device_listener_catalog_patch_echo_keeps_enabled", "[device_listener]")
{
    start_listener(true);
    EMIT({"put", SNAPSHOT_ALL_ON},
         {"patch", "{\"path\":\"/sensors\",\"data\":{\"temperature_1/type\":\"temperature\","
                   "\"temperature_1/simulated\":true}}"});

    TEST_ASSERT_TRUE(sensor_provider_is_enabled(SENSOR_TEMPERATURE_1));
}

/* ---- Other events and invalid input ---- */

TEST_CASE("test_device_listener_keep_alive_cancel_and_auth_revoked_change_nothing",
          "[device_listener]")
{
    start_listener(true);
    EMIT({"put", SNAPSHOT_ALL_ON}, {"keep-alive", "null"}, {"cancel", "\"Permission denied\""},
         {"auth_revoked", "\"credential is no longer valid\""});

    TEST_ASSERT_TRUE(sensor_provider_is_enabled(SENSOR_TEMPERATURE_1));
    TEST_ASSERT_TRUE(actuator_controller_is_on(ACTUATOR_RGB_LIGHT_1));
}

TEST_CASE("test_device_listener_invalid_json_is_skipped", "[device_listener]")
{
    start_listener(true);
    EMIT({"put", "not json"});

    TEST_ASSERT_FALSE(sensor_provider_is_enabled(SENSOR_TEMPERATURE_1));
}

TEST_CASE("test_device_listener_event_without_path_is_skipped", "[device_listener]")
{
    start_listener(true);
    EMIT({"put", "{\"data\":null}"});

    TEST_ASSERT_FALSE(sensor_provider_is_enabled(SENSOR_TEMPERATURE_1));
}

TEST_CASE("test_device_listener_path_longer_than_buffer_is_skipped", "[device_listener]")
{
    char long_path[LONG_PATH_LEN + 1U] = {0};
    memset(long_path, 'a', LONG_PATH_LEN);
    (void)snprintf(s_long_path_event, sizeof(s_long_path_event), "{\"path\":\"/%s\",\"data\":null}",
                   long_path);

    start_listener(true);
    EMIT({"put", SNAPSHOT_ALL_ON}, {"put", s_long_path_event});

    TEST_ASSERT_TRUE(sensor_provider_is_enabled(SENSOR_TEMPERATURE_1));
}

TEST_CASE("test_device_listener_bad_event_does_not_stop_dispatching", "[device_listener]")
{
    start_listener(true);
    EMIT({"put", "not json"},
         {"put", "{\"path\":\"/sensors/luminosity_2/enabled\",\"data\":true}"});

    TEST_ASSERT_TRUE(sensor_provider_is_enabled(SENSOR_LUMINOSITY_2));
}
