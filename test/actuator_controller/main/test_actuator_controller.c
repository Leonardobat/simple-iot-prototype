#include <stdint.h>
#include <string.h>

#include "actuator_controller.h"
#include "fake_gpio_output.h"
#include "fake_rgb_led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"

/* Real actuator_controller over the fake rgb_led and gpio_output. See sdkconfig.defaults: light_1
 * on GPIO6 (active high), light_2 without output; relay_1 on GPIO9 and the curtain on GPIO18 (up)
 * and GPIO21 (down), both active low (relay modules); relay_2 without output. */

#define LIGHT_1_GPIO      6U
#define RELAY_1_GPIO      9U
#define CURTAIN_UP_GPIO   18U
#define CURTAIN_DOWN_GPIO 21U
#define REVERSE_DELAY_MS  20U /* CONFIG_ACTUATOR_CURTAIN_REVERSE_DELAY_MS of this project. */

static char s_json[ACTUATOR_CATALOG_JSON_MAX_LEN];

static void switch_on(actuator_t actuator)
{
    actuator_controller_set_enabled(actuator, true);
    if (actuator_controller_info(actuator)->kind == ACTUATOR_KIND_CURTAIN) {
        actuator_controller_set_curtain(actuator, "up");
    } else {
        actuator_controller_set_command(actuator, true);
    }
}

/* Relays are active low: the pad is low while the relay is on. */
static bool relay_is_on(uint32_t gpio)
{
    return !fake_gpio_output.high[gpio];
}

static bool led_is_white(void)
{
    return (fake_rgb_led.red > 0U) && (fake_rgb_led.red == fake_rgb_led.green) &&
           (fake_rgb_led.green == fake_rgb_led.blue);
}

static bool led_is_off(void)
{
    return (fake_rgb_led.red == 0U) && (fake_rgb_led.green == 0U) && (fake_rgb_led.blue == 0U);
}

void setUp(void)
{
    memset(s_json, 0, sizeof(s_json));
    fake_rgb_led_reset();
    fake_gpio_output_reset();
    TEST_ASSERT_EQUAL_INT32(ESP_OK, actuator_controller_init());
}

void tearDown(void)
{
    actuator_controller_deinit();
}

/* ---- Lifecycle ---- */

TEST_CASE("test_actuator_controller_init_starts_disabled_and_outputs_off", "[actuator_controller]")
{
    TEST_ASSERT_EQUAL_UINT32(1U, fake_rgb_led.init_calls);
    /* light_1, relay_1 and the two curtain relays; light_2 and relay_2 have no output. */
    TEST_ASSERT_EQUAL_UINT32(4U, fake_gpio_output.init_calls);
    TEST_ASSERT_TRUE(fake_gpio_output.configured[LIGHT_1_GPIO]);
    TEST_ASSERT_TRUE(fake_gpio_output.pull_up[LIGHT_1_GPIO]); /* ACTUATOR_LIGHT_PULL_UP=y. */
    TEST_ASSERT_FALSE(fake_gpio_output.high[LIGHT_1_GPIO]);
    const uint32_t relays[] = {RELAY_1_GPIO, CURTAIN_UP_GPIO, CURTAIN_DOWN_GPIO};
    for (size_t i = 0U; i < (sizeof(relays) / sizeof(relays[0])); i++) {
        TEST_ASSERT_TRUE(fake_gpio_output.configured[relays[i]]);
        TEST_ASSERT_TRUE(fake_gpio_output.pull_up[relays[i]]); /* Active low: the pull-up is off. */
        TEST_ASSERT_FALSE(relay_is_on(relays[i]));             /* Started off, with no pulse. */
    }
    for (uint32_t i = 0U; i < (uint32_t)ACTUATOR_COUNT; i++) {
        TEST_ASSERT_FALSE(actuator_controller_is_enabled((actuator_t)i));
        TEST_ASSERT_FALSE(actuator_controller_is_on((actuator_t)i));
    }
}

TEST_CASE("test_actuator_controller_init_failure_releases_and_reinit_works",
          "[actuator_controller]")
{
    actuator_controller_deinit();
    fake_gpio_output.init_result = ESP_ERR_INVALID_ARG;

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_ARG, actuator_controller_init());
    TEST_ASSERT_EQUAL_UINT32(2U, fake_rgb_led.deinit_calls); /* First deinit + cleanup. */

    fake_gpio_output.init_result = ESP_OK;
    TEST_ASSERT_EQUAL_INT32(ESP_OK, actuator_controller_init());
}

TEST_CASE("test_actuator_controller_deinit_switches_outputs_off", "[actuator_controller]")
{
    switch_on(ACTUATOR_RGB_LIGHT_1);

    actuator_controller_deinit();

    TEST_ASSERT_TRUE(led_is_off());
    TEST_ASSERT_EQUAL_UINT32(4U, fake_gpio_output.deinit_calls); /* light_1, relay_1, curtain. */
    TEST_ASSERT_FALSE(fake_gpio_output.configured[LIGHT_1_GPIO]);
    TEST_ASSERT_FALSE(actuator_controller_is_on(ACTUATOR_RGB_LIGHT_1));
    TEST_ASSERT_EQUAL_INT32(ESP_OK, actuator_controller_init()); /* For tearDown. */
}

TEST_CASE("test_actuator_controller_reinit_resets_states", "[actuator_controller]")
{
    switch_on(ACTUATOR_LIGHT_1);
    actuator_controller_deinit();

    TEST_ASSERT_EQUAL_INT32(ESP_OK, actuator_controller_init());
    TEST_ASSERT_FALSE(actuator_controller_is_enabled(ACTUATOR_LIGHT_1));
    TEST_ASSERT_FALSE(actuator_controller_is_on(ACTUATOR_LIGHT_1));
}

/* ---- RGB light (rgb_led) ---- */

TEST_CASE("test_actuator_controller_rgb_light_on_shows_white", "[actuator_controller]")
{
    switch_on(ACTUATOR_RGB_LIGHT_1);

    TEST_ASSERT_TRUE(actuator_controller_is_on(ACTUATOR_RGB_LIGHT_1));
    TEST_ASSERT_TRUE(led_is_white());
    TEST_ASSERT_FALSE(fake_gpio_output.high[LIGHT_1_GPIO]); /* The plain light is untouched. */
}

TEST_CASE("test_actuator_controller_rgb_light_command_off_switches_led_off",
          "[actuator_controller]")
{
    switch_on(ACTUATOR_RGB_LIGHT_1);
    actuator_controller_set_command(ACTUATOR_RGB_LIGHT_1, false);

    TEST_ASSERT_FALSE(actuator_controller_is_on(ACTUATOR_RGB_LIGHT_1));
    TEST_ASSERT_TRUE(led_is_off());
}

TEST_CASE("test_actuator_controller_command_while_disabled_keeps_led_off", "[actuator_controller]")
{
    actuator_controller_set_command(ACTUATOR_RGB_LIGHT_1, true);

    TEST_ASSERT_FALSE(actuator_controller_is_on(ACTUATOR_RGB_LIGHT_1));
    TEST_ASSERT_EQUAL_UINT32(0U, fake_rgb_led.set_calls);
}

TEST_CASE("test_actuator_controller_enable_after_command_turns_on", "[actuator_controller]")
{
    actuator_controller_set_command(ACTUATOR_RGB_LIGHT_1, true);
    actuator_controller_set_enabled(ACTUATOR_RGB_LIGHT_1, true);

    TEST_ASSERT_TRUE(led_is_white());
}

TEST_CASE("test_actuator_controller_disable_turns_led_off", "[actuator_controller]")
{
    switch_on(ACTUATOR_RGB_LIGHT_1);
    actuator_controller_set_enabled(ACTUATOR_RGB_LIGHT_1, false);

    TEST_ASSERT_FALSE(actuator_controller_is_on(ACTUATOR_RGB_LIGHT_1));
    TEST_ASSERT_TRUE(led_is_off());
}

TEST_CASE("test_actuator_controller_repeated_command_drives_output_once", "[actuator_controller]")
{
    switch_on(ACTUATOR_RGB_LIGHT_1);
    actuator_controller_set_command(ACTUATOR_RGB_LIGHT_1, true);

    TEST_ASSERT_EQUAL_UINT32(1U, fake_rgb_led.set_calls);
}

TEST_CASE("test_actuator_controller_driver_error_reports_real_state_and_retries",
          "[actuator_controller]")
{
    fake_rgb_led.set_result = ESP_FAIL;
    switch_on(ACTUATOR_RGB_LIGHT_1);
    TEST_ASSERT_FALSE(actuator_controller_is_on(ACTUATOR_RGB_LIGHT_1));
    TEST_ASSERT_EQUAL_INT32(ESP_OK, actuator_controller_states_to_json(s_json, sizeof(s_json)));
    TEST_ASSERT_EQUAL_STRING("{\"rgb_light_1\":{\"on\":false,\"color\":\"#f0f0f0\"}}", s_json);

    fake_rgb_led.set_result = ESP_OK;
    actuator_controller_set_command(ACTUATOR_RGB_LIGHT_1, true);
    TEST_ASSERT_TRUE(actuator_controller_is_on(ACTUATOR_RGB_LIGHT_1));
}

/* ---- RGB colour ---- */

static void assert_led(uint8_t red, uint8_t green, uint8_t blue)
{
    TEST_ASSERT_EQUAL_UINT32(red, fake_rgb_led.red);
    TEST_ASSERT_EQUAL_UINT32(green, fake_rgb_led.green);
    TEST_ASSERT_EQUAL_UINT32(blue, fake_rgb_led.blue);
}

TEST_CASE("test_actuator_controller_only_rgb_light_has_color", "[actuator_controller]")
{
    TEST_ASSERT_EQUAL_INT32(ACTUATOR_KIND_RGB,
                            actuator_controller_info(ACTUATOR_RGB_LIGHT_1)->kind);
    TEST_ASSERT_EQUAL_INT32(ACTUATOR_KIND_SWITCH, actuator_controller_info(ACTUATOR_LIGHT_1)->kind);
    TEST_ASSERT_EQUAL_INT32(ACTUATOR_KIND_SWITCH, actuator_controller_info(ACTUATOR_RELAY_1)->kind);
    TEST_ASSERT_EQUAL_INT32(ACTUATOR_KIND_CURTAIN,
                            actuator_controller_info(ACTUATOR_CURTAIN_1)->kind);
}

TEST_CASE("test_actuator_controller_rgb_light_default_color_is_white", "[actuator_controller]")
{
    switch_on(ACTUATOR_RGB_LIGHT_1);

    assert_led(0xF0U, 0xF0U, 0xF0U);
    TEST_ASSERT_EQUAL_HEX32(0xF0F0F0U, actuator_controller_get_color(ACTUATOR_RGB_LIGHT_1));
}

TEST_CASE("test_actuator_controller_color_while_on_changes_led", "[actuator_controller]")
{
    switch_on(ACTUATOR_RGB_LIGHT_1);
    actuator_controller_set_color(ACTUATOR_RGB_LIGHT_1, "#ff8000");

    assert_led(0xFFU, 0x80U, 0x00U);
    TEST_ASSERT_EQUAL_HEX32(0xFF8000U, actuator_controller_get_color(ACTUATOR_RGB_LIGHT_1));
}

TEST_CASE("test_actuator_controller_color_while_off_is_shown_when_on", "[actuator_controller]")
{
    actuator_controller_set_color(ACTUATOR_RGB_LIGHT_1, "#0000ff");
    TEST_ASSERT_EQUAL_UINT32(0U, fake_rgb_led.set_calls);

    switch_on(ACTUATOR_RGB_LIGHT_1);
    assert_led(0x00U, 0x00U, 0xFFU);
}

TEST_CASE("test_actuator_controller_color_upper_case_hex_is_accepted", "[actuator_controller]")
{
    switch_on(ACTUATOR_RGB_LIGHT_1);
    actuator_controller_set_color(ACTUATOR_RGB_LIGHT_1, "#A0B1C2");

    assert_led(0xA0U, 0xB1U, 0xC2U);
}

TEST_CASE("test_actuator_controller_invalid_color_uses_default", "[actuator_controller]")
{
    static const char *const invalid[] = {"ff8000",  "#ff80",   "#ff80001",
                                          "#gg0000", "#ff 800", "red"};
    switch_on(ACTUATOR_RGB_LIGHT_1);

    for (size_t i = 0U; i < (sizeof(invalid) / sizeof(invalid[0])); i++) {
        actuator_controller_set_color(ACTUATOR_RGB_LIGHT_1, "#ff0000");
        actuator_controller_set_color(ACTUATOR_RGB_LIGHT_1, invalid[i]);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(
            0xF0F0F0U, actuator_controller_get_color(ACTUATOR_RGB_LIGHT_1), invalid[i]);
    }
}

TEST_CASE("test_actuator_controller_empty_color_restores_default", "[actuator_controller]")
{
    switch_on(ACTUATOR_RGB_LIGHT_1);
    actuator_controller_set_color(ACTUATOR_RGB_LIGHT_1, "#00ff00");
    actuator_controller_set_color(ACTUATOR_RGB_LIGHT_1, "");

    assert_led(0xF0U, 0xF0U, 0xF0U);
}

TEST_CASE("test_actuator_controller_same_color_drives_led_once", "[actuator_controller]")
{
    switch_on(ACTUATOR_RGB_LIGHT_1);
    actuator_controller_set_color(ACTUATOR_RGB_LIGHT_1, "#123456");
    const uint32_t set_calls = fake_rgb_led.set_calls;

    actuator_controller_set_color(ACTUATOR_RGB_LIGHT_1, "#123456");
    TEST_ASSERT_EQUAL_UINT32(set_calls, fake_rgb_led.set_calls);
}

TEST_CASE("test_actuator_controller_color_driver_error_keeps_shown_color", "[actuator_controller]")
{
    switch_on(ACTUATOR_RGB_LIGHT_1);
    actuator_controller_set_color(ACTUATOR_RGB_LIGHT_1, "#ff0000");
    fake_rgb_led.set_result = ESP_FAIL;

    actuator_controller_set_color(ACTUATOR_RGB_LIGHT_1, "#00ff00");
    TEST_ASSERT_EQUAL_HEX32(0xFF0000U, actuator_controller_get_color(ACTUATOR_RGB_LIGHT_1));
    TEST_ASSERT_TRUE(actuator_controller_is_on(ACTUATOR_RGB_LIGHT_1));
}

TEST_CASE("test_actuator_controller_states_json_reports_rgb_on_and_color", "[actuator_controller]")
{
    switch_on(ACTUATOR_RGB_LIGHT_1);
    actuator_controller_set_color(ACTUATOR_RGB_LIGHT_1, "#FF8000");

    TEST_ASSERT_EQUAL_INT32(ESP_OK, actuator_controller_states_to_json(s_json, sizeof(s_json)));
    TEST_ASSERT_EQUAL_STRING("{\"rgb_light_1\":{\"on\":true,\"color\":\"#ff8000\"}}", s_json);
}

TEST_CASE("test_actuator_controller_reinit_restores_default_color", "[actuator_controller]")
{
    switch_on(ACTUATOR_RGB_LIGHT_1);
    actuator_controller_set_color(ACTUATOR_RGB_LIGHT_1, "#ff0000");
    actuator_controller_deinit();

    TEST_ASSERT_EQUAL_INT32(ESP_OK, actuator_controller_init());
    TEST_ASSERT_EQUAL_HEX32(0xF0F0F0U, actuator_controller_get_color(ACTUATOR_RGB_LIGHT_1));
}

/* ---- Plain lights (gpio_output) ---- */

TEST_CASE("test_actuator_controller_light_on_drives_its_gpio_high", "[actuator_controller]")
{
    switch_on(ACTUATOR_LIGHT_1);

    TEST_ASSERT_TRUE(actuator_controller_is_on(ACTUATOR_LIGHT_1));
    TEST_ASSERT_TRUE(fake_gpio_output.high[LIGHT_1_GPIO]);
    TEST_ASSERT_TRUE(led_is_off()); /* The RGB light is untouched. */
}

TEST_CASE("test_actuator_controller_light_off_drives_gpio_low", "[actuator_controller]")
{
    switch_on(ACTUATOR_LIGHT_1);
    actuator_controller_set_command(ACTUATOR_LIGHT_1, false);

    TEST_ASSERT_FALSE(fake_gpio_output.high[LIGHT_1_GPIO]);
}

TEST_CASE("test_actuator_controller_light_without_output_is_simulated", "[actuator_controller]")
{
    const uint32_t set_calls = fake_gpio_output.set_calls;
    switch_on(ACTUATOR_LIGHT_2);

    TEST_ASSERT_TRUE(actuator_controller_is_on(ACTUATOR_LIGHT_2));
    TEST_ASSERT_EQUAL_UINT32(set_calls, fake_gpio_output.set_calls);
    TEST_ASSERT_TRUE(actuator_controller_info(ACTUATOR_LIGHT_2)->simulated);
}

TEST_CASE("test_actuator_controller_gpio_error_keeps_light_off", "[actuator_controller]")
{
    fake_gpio_output.set_result = ESP_FAIL;
    switch_on(ACTUATOR_LIGHT_1);

    TEST_ASSERT_FALSE(actuator_controller_is_on(ACTUATOR_LIGHT_1));
}

/* ---- Relays ---- */

TEST_CASE("test_actuator_controller_relay_on_drives_its_gpio_low", "[actuator_controller]")
{
    switch_on(ACTUATOR_RELAY_1);

    TEST_ASSERT_TRUE(actuator_controller_is_on(ACTUATOR_RELAY_1));
    TEST_ASSERT_TRUE(relay_is_on(RELAY_1_GPIO));
}

TEST_CASE("test_actuator_controller_relay_off_drives_gpio_high", "[actuator_controller]")
{
    switch_on(ACTUATOR_RELAY_1);
    actuator_controller_set_command(ACTUATOR_RELAY_1, false);

    TEST_ASSERT_FALSE(actuator_controller_is_on(ACTUATOR_RELAY_1));
    TEST_ASSERT_FALSE(relay_is_on(RELAY_1_GPIO));
}

TEST_CASE("test_actuator_controller_relay_without_output_is_simulated", "[actuator_controller]")
{
    const uint32_t set_calls = fake_gpio_output.set_calls;
    switch_on(ACTUATOR_RELAY_2);

    TEST_ASSERT_TRUE(actuator_controller_is_on(ACTUATOR_RELAY_2));
    TEST_ASSERT_EQUAL_UINT32(set_calls, fake_gpio_output.set_calls);
    TEST_ASSERT_TRUE(actuator_controller_info(ACTUATOR_RELAY_2)->simulated);
}

/* ---- Curtain (two relays) ---- */

static void enable_curtain(void)
{
    actuator_controller_set_enabled(ACTUATOR_CURTAIN_1, true);
}

TEST_CASE("test_actuator_controller_curtain_up_engages_only_the_up_relay", "[actuator_controller]")
{
    enable_curtain();
    actuator_controller_set_curtain(ACTUATOR_CURTAIN_1, "up");

    TEST_ASSERT_EQUAL_INT32(ACTUATOR_CURTAIN_UP,
                            actuator_controller_get_curtain(ACTUATOR_CURTAIN_1));
    TEST_ASSERT_TRUE(actuator_controller_is_on(ACTUATOR_CURTAIN_1));
    TEST_ASSERT_TRUE(relay_is_on(CURTAIN_UP_GPIO));
    TEST_ASSERT_FALSE(relay_is_on(CURTAIN_DOWN_GPIO));
}

TEST_CASE("test_actuator_controller_curtain_down_engages_only_the_down_relay",
          "[actuator_controller]")
{
    enable_curtain();
    actuator_controller_set_curtain(ACTUATOR_CURTAIN_1, "down");

    TEST_ASSERT_EQUAL_INT32(ACTUATOR_CURTAIN_DOWN,
                            actuator_controller_get_curtain(ACTUATOR_CURTAIN_1));
    TEST_ASSERT_FALSE(relay_is_on(CURTAIN_UP_GPIO));
    TEST_ASSERT_TRUE(relay_is_on(CURTAIN_DOWN_GPIO));
}

TEST_CASE("test_actuator_controller_curtain_reversal_releases_before_engaging",
          "[actuator_controller]")
{
    enable_curtain();
    actuator_controller_set_curtain(ACTUATOR_CURTAIN_1, "up");
    const uint32_t first = fake_gpio_output.log_len;

    actuator_controller_set_curtain(ACTUATOR_CURTAIN_1, "down");

    /* Two writes, in this order: the up relay off, then the down relay on. */
    TEST_ASSERT_EQUAL_UINT32(first + 2U, fake_gpio_output.log_len);
    TEST_ASSERT_EQUAL_INT32(CURTAIN_UP_GPIO, fake_gpio_output.log[first].gpio);
    TEST_ASSERT_TRUE(fake_gpio_output.log[first].high); /* Active low: high is off. */
    TEST_ASSERT_EQUAL_INT32(CURTAIN_DOWN_GPIO, fake_gpio_output.log[first + 1U].gpio);
    TEST_ASSERT_FALSE(fake_gpio_output.log[first + 1U].high);
}

TEST_CASE("test_actuator_controller_curtain_reversal_waits_the_reverse_delay",
          "[actuator_controller]")
{
    enable_curtain();
    actuator_controller_set_curtain(ACTUATOR_CURTAIN_1, "up");

    const TickType_t start = xTaskGetTickCount();
    actuator_controller_set_curtain(ACTUATOR_CURTAIN_1, "down");

    TEST_ASSERT_TRUE((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(REVERSE_DELAY_MS));
}

TEST_CASE("test_actuator_controller_curtain_stop_releases_both_relays", "[actuator_controller]")
{
    enable_curtain();
    actuator_controller_set_curtain(ACTUATOR_CURTAIN_1, "up");
    actuator_controller_set_curtain(ACTUATOR_CURTAIN_1, "stop");

    TEST_ASSERT_EQUAL_INT32(ACTUATOR_CURTAIN_STOP,
                            actuator_controller_get_curtain(ACTUATOR_CURTAIN_1));
    TEST_ASSERT_FALSE(actuator_controller_is_on(ACTUATOR_CURTAIN_1));
    TEST_ASSERT_FALSE(relay_is_on(CURTAIN_UP_GPIO));
    TEST_ASSERT_FALSE(relay_is_on(CURTAIN_DOWN_GPIO));
}

TEST_CASE("test_actuator_controller_curtain_stop_does_not_wait", "[actuator_controller]")
{
    enable_curtain();
    actuator_controller_set_curtain(ACTUATOR_CURTAIN_1, "up");

    const TickType_t start = xTaskGetTickCount();
    actuator_controller_set_curtain(ACTUATOR_CURTAIN_1, "stop");

    TEST_ASSERT_TRUE((xTaskGetTickCount() - start) < pdMS_TO_TICKS(REVERSE_DELAY_MS));
}

TEST_CASE("test_actuator_controller_curtain_invalid_or_empty_command_stops",
          "[actuator_controller]")
{
    static const char *const stopping[] = {"", "sideways", "UP", "u"};
    enable_curtain();

    for (size_t i = 0U; i < (sizeof(stopping) / sizeof(stopping[0])); i++) {
        actuator_controller_set_curtain(ACTUATOR_CURTAIN_1, "up");
        actuator_controller_set_curtain(ACTUATOR_CURTAIN_1, stopping[i]);
        TEST_ASSERT_EQUAL_INT32_MESSAGE(ACTUATOR_CURTAIN_STOP,
                                        actuator_controller_get_curtain(ACTUATOR_CURTAIN_1),
                                        stopping[i]);
    }
}

TEST_CASE("test_actuator_controller_curtain_disabled_stays_stopped_until_enabled",
          "[actuator_controller]")
{
    actuator_controller_set_curtain(ACTUATOR_CURTAIN_1, "down");
    TEST_ASSERT_EQUAL_INT32(ACTUATOR_CURTAIN_STOP,
                            actuator_controller_get_curtain(ACTUATOR_CURTAIN_1));

    enable_curtain(); /* The command was remembered. */
    TEST_ASSERT_EQUAL_INT32(ACTUATOR_CURTAIN_DOWN,
                            actuator_controller_get_curtain(ACTUATOR_CURTAIN_1));

    actuator_controller_set_enabled(ACTUATOR_CURTAIN_1, false);
    TEST_ASSERT_EQUAL_INT32(ACTUATOR_CURTAIN_STOP,
                            actuator_controller_get_curtain(ACTUATOR_CURTAIN_1));
    TEST_ASSERT_FALSE(relay_is_on(CURTAIN_DOWN_GPIO));
}

TEST_CASE("test_actuator_controller_curtain_same_command_drives_relays_once",
          "[actuator_controller]")
{
    enable_curtain();
    actuator_controller_set_curtain(ACTUATOR_CURTAIN_1, "up");
    const uint32_t set_calls = fake_gpio_output.set_calls;

    actuator_controller_set_curtain(ACTUATOR_CURTAIN_1, "up");
    TEST_ASSERT_EQUAL_UINT32(set_calls, fake_gpio_output.set_calls);
}

TEST_CASE("test_actuator_controller_curtain_driver_error_reports_stop", "[actuator_controller]")
{
    enable_curtain();
    actuator_controller_set_curtain(ACTUATOR_CURTAIN_1, "up");
    fake_gpio_output.set_result = ESP_FAIL;

    actuator_controller_set_curtain(ACTUATOR_CURTAIN_1, "down");

    TEST_ASSERT_EQUAL_INT32(ACTUATOR_CURTAIN_STOP,
                            actuator_controller_get_curtain(ACTUATOR_CURTAIN_1));
}

TEST_CASE("test_actuator_controller_curtain_deinit_releases_both_relays", "[actuator_controller]")
{
    enable_curtain();
    actuator_controller_set_curtain(ACTUATOR_CURTAIN_1, "up");

    actuator_controller_deinit();

    TEST_ASSERT_FALSE(fake_gpio_output.configured[CURTAIN_UP_GPIO]);
    TEST_ASSERT_FALSE(fake_gpio_output.configured[CURTAIN_DOWN_GPIO]);
    TEST_ASSERT_EQUAL_INT32(ESP_OK, actuator_controller_init()); /* For tearDown. */
}

TEST_CASE("test_actuator_controller_states_json_reports_curtain_direction", "[actuator_controller]")
{
    enable_curtain();
    actuator_controller_set_curtain(ACTUATOR_CURTAIN_1, "down");

    TEST_ASSERT_EQUAL_INT32(ESP_OK, actuator_controller_states_to_json(s_json, sizeof(s_json)));
    TEST_ASSERT_EQUAL_STRING("{\"curtain_1\":\"down\"}", s_json);
}

/* ---- JSON ---- */

TEST_CASE("test_actuator_controller_info_ids_are_unique", "[actuator_controller]")
{
    for (uint32_t i = 0U; i < (uint32_t)ACTUATOR_COUNT; i++) {
        for (uint32_t j = i + 1U; j < (uint32_t)ACTUATOR_COUNT; j++) {
            TEST_ASSERT_NOT_EQUAL_INT32(0, strcmp(actuator_controller_info((actuator_t)i)->id,
                                                  actuator_controller_info((actuator_t)j)->id));
        }
    }
}

TEST_CASE("test_actuator_controller_states_json_lists_only_enabled", "[actuator_controller]")
{
    actuator_controller_set_enabled(ACTUATOR_LIGHT_2, true);

    TEST_ASSERT_EQUAL_INT32(ESP_OK, actuator_controller_states_to_json(s_json, sizeof(s_json)));
    TEST_ASSERT_EQUAL_STRING("{\"light_2\":false}", s_json);
}

TEST_CASE("test_actuator_controller_states_json_none_enabled_is_empty", "[actuator_controller]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, actuator_controller_states_to_json(s_json, sizeof(s_json)));
    TEST_ASSERT_EQUAL_STRING("{}", s_json);
}

TEST_CASE("test_actuator_controller_states_json_all_enabled_fits_max_len", "[actuator_controller]")
{
    char json[ACTUATOR_STATES_JSON_MAX_LEN] = {0};
    for (uint32_t i = 0U; i < (uint32_t)ACTUATOR_COUNT; i++) {
        switch_on((actuator_t)i);
    }

    TEST_ASSERT_EQUAL_INT32(ESP_OK, actuator_controller_states_to_json(json, sizeof(json)));
}

TEST_CASE("test_actuator_controller_snapshot_json_matches_the_live_states_json",
          "[actuator_controller]")
{
    char live[ACTUATOR_STATES_JSON_MAX_LEN] = {0};
    actuator_snapshot_t snapshot = {0};
    enable_curtain();
    actuator_controller_set_curtain(ACTUATOR_CURTAIN_1, "up");
    switch_on(ACTUATOR_RGB_LIGHT_1);
    actuator_controller_set_enabled(ACTUATOR_LIGHT_2, true);
    TEST_ASSERT_EQUAL_INT32(ESP_OK, actuator_controller_states_to_json(live, sizeof(live)));

    actuator_controller_snapshot(&snapshot);
    actuator_controller_set_enabled(ACTUATOR_LIGHT_2, false); /* The snapshot does not follow. */

    TEST_ASSERT_EQUAL_INT32(ESP_OK, actuator_snapshot_to_json(&snapshot, s_json, sizeof(s_json)));
    TEST_ASSERT_EQUAL_STRING(live, s_json);
}

TEST_CASE("test_actuator_controller_snapshot_of_disabled_actuators_is_zero",
          "[actuator_controller]")
{
    actuator_snapshot_t snapshot = {0};
    snapshot.state[0] = 0xFFFFFFFFU;

    actuator_controller_snapshot(&snapshot);

    for (uint32_t i = 0U; i < (uint32_t)ACTUATOR_COUNT; i++) {
        TEST_ASSERT_EQUAL_HEX32(0U, snapshot.state[i]);
    }
}

TEST_CASE("test_actuator_controller_snapshot_json_corrupt_curtain_state_reads_stop",
          "[actuator_controller]")
{
    actuator_snapshot_t snapshot = {0};
    snapshot.state[ACTUATOR_CURTAIN_1] = 0x80000000U | 0x7FU;

    TEST_ASSERT_EQUAL_INT32(ESP_OK, actuator_snapshot_to_json(&snapshot, s_json, sizeof(s_json)));
    TEST_ASSERT_EQUAL_STRING("{\"curtain_1\":\"stop\"}", s_json);
}

TEST_CASE("test_actuator_controller_snapshot_json_small_buffer_reports_invalid_size",
          "[actuator_controller]")
{
    actuator_snapshot_t snapshot = {0};
    char tiny[4] = {0};
    snapshot.state[ACTUATOR_LIGHT_1] = 0x80000000U;

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_SIZE,
                            actuator_snapshot_to_json(&snapshot, tiny, sizeof(tiny)));
}

TEST_CASE("test_actuator_controller_catalog_marks_only_undriven_simulated", "[actuator_controller]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, actuator_controller_catalog_to_json(s_json, sizeof(s_json)));

    TEST_ASSERT_EQUAL_STRING("{\"light_1/type\":\"light\",\"light_1/simulated\":false,"
                             "\"light_2/type\":\"light\",\"light_2/simulated\":true,"
                             "\"rgb_light_1/type\":\"rgb_light\",\"rgb_light_1/simulated\":false,"
                             "\"curtain_1/type\":\"curtain\",\"curtain_1/simulated\":false,"
                             "\"relay_1/type\":\"relay\",\"relay_1/simulated\":false,"
                             "\"relay_2/type\":\"relay\",\"relay_2/simulated\":true}",
                             s_json);
}

TEST_CASE("test_actuator_controller_catalog_fits_max_len", "[actuator_controller]")
{
    char json[ACTUATOR_CATALOG_JSON_MAX_LEN] = {0};

    TEST_ASSERT_EQUAL_INT32(ESP_OK, actuator_controller_catalog_to_json(json, sizeof(json)));
}

TEST_CASE("test_actuator_controller_catalog_small_buffer_returns_invalid_size",
          "[actuator_controller]")
{
    char small[16] = {0};

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_SIZE,
                            actuator_controller_catalog_to_json(small, sizeof(small)));
}
