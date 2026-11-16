#include "actuator_controller.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "actuator_controller_config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_output.h"
#include "json_writer.h"
#include "rgb_led.h"
#include "utils.h"

static const char *TAG = "actuator_ctrl";

/* Physical output of one actuator. */
typedef enum {
    OUTPUT_NONE = 0, /* Simulated: the state is only recorded. */
    OUTPUT_RGB_LED,  /* rgb_led, in the actuator colour. */
    OUTPUT_GPIO,     /* gpio_output on `gpio`. */
    OUTPUT_CURTAIN,  /* gpio_output on `gpio` (up) and `gpio_down` (down). */
} output_kind_t;

typedef struct {
    output_kind_t kind;
    int32_t gpio;      /* OUTPUT_GPIO; the "up" relay of OUTPUT_CURTAIN. */
    int32_t gpio_down; /* Only for OUTPUT_CURTAIN. */
    bool on_level;     /* GPIO level that switches the load on. */
    bool pull_up;      /* Internal pull-up while the pad is not driven. */
} output_t;

/* One actuator: what it is (published) and how it is driven. */
typedef struct {
    actuator_info_t info;
    output_t output;
} actuator_entry_t;

/* A switch with a negative GPIO has no output and stays simulated. */
#define SWITCH_ENTRY(switch_id, switch_type, gpio, on_lvl, pull)                                   \
    {                                                                                              \
        .info = {switch_id, switch_type, (gpio) < 0, ACTUATOR_KIND_SWITCH},                        \
        .output = {((gpio) >= 0) ? OUTPUT_GPIO : OUTPUT_NONE, (gpio), -1, (on_lvl), (pull)},       \
    }

#define LIGHT_ENTRY(light_id, gpio)                                                                \
    SWITCH_ENTRY(light_id, "light", gpio, ACTUATOR_LIGHT_ON_LEVEL, ACTUATOR_LIGHT_PULL_UP)

#define RELAY_ENTRY(relay_id, gpio)                                                                \
    SWITCH_ENTRY(relay_id, "relay", gpio, ACTUATOR_RELAY_ON_LEVEL, ACTUATOR_RELAY_PULL_UP)

/* The curtain needs both relays; with one missing it stays simulated. */
#define CURTAIN_HAS_OUTPUT ((ACTUATOR_CURTAIN_UP_GPIO >= 0) && (ACTUATOR_CURTAIN_DOWN_GPIO >= 0))

/* Indexed by actuator_t. */
static const actuator_entry_t s_actuators[ACTUATOR_COUNT] = {
    [ACTUATOR_LIGHT_1] = LIGHT_ENTRY("light_1", ACTUATOR_LIGHT_1_GPIO),
    [ACTUATOR_LIGHT_2] = LIGHT_ENTRY("light_2", ACTUATOR_LIGHT_2_GPIO),
    [ACTUATOR_RGB_LIGHT_1] =
        {
            .info = {"rgb_light_1", "rgb_light", false, ACTUATOR_KIND_RGB},
            .output = {OUTPUT_RGB_LED, -1, -1, true, false},
        },
    [ACTUATOR_CURTAIN_1] =
        {
            .info = {"curtain_1", "curtain", !CURTAIN_HAS_OUTPUT, ACTUATOR_KIND_CURTAIN},
            .output = {CURTAIN_HAS_OUTPUT ? OUTPUT_CURTAIN : OUTPUT_NONE, ACTUATOR_CURTAIN_UP_GPIO,
                       ACTUATOR_CURTAIN_DOWN_GPIO, ACTUATOR_RELAY_ON_LEVEL, ACTUATOR_RELAY_PULL_UP},
        },
    [ACTUATOR_RELAY_1] = RELAY_ENTRY("relay_1", ACTUATOR_RELAY_1_GPIO),
    [ACTUATOR_RELAY_2] = RELAY_ENTRY("relay_2", ACTUATOR_RELAY_2_GPIO),
};

/* Text of each actuator_curtain_t, indexed by it. */
static const char *const s_curtain_text[] = {
    [ACTUATOR_CURTAIN_STOP] = ACTUATOR_CURTAIN_TEXT_STOP,
    [ACTUATOR_CURTAIN_UP] = ACTUATOR_CURTAIN_TEXT_UP,
    [ACTUATOR_CURTAIN_DOWN] = ACTUATOR_CURTAIN_TEXT_DOWN,
};
#define CURTAIN_STATE_COUNT (sizeof(s_curtain_text) / sizeof(s_curtain_text[0]))

static bool s_initialized;
/* What each actuator is asked and really does. A switch is 0 (off) or 1 (on); a curtain holds an
 * actuator_curtain_t. Written by the device_listener task, read by the publishing task. */
static atomic_bool s_enabled[ACTUATOR_COUNT];
static atomic_uint_least32_t s_command[ACTUATOR_COUNT];
static atomic_uint_least32_t s_output_state[ACTUATOR_COUNT];
/* Colour (0xRRGGBB) of the RGB lights: requested, and last shown. */
static atomic_uint_least32_t s_color[ACTUATOR_COUNT];
static atomic_uint_least32_t s_output_color[ACTUATOR_COUNT];

static uint8_t channel(uint32_t color, uint32_t shift)
{
    return (uint8_t)((color >> shift) & ACTUATOR_COLOR_CHANNEL_MASK);
}

/* ---- Outputs ---- */

static esp_err_t set_relay(const output_t *output, int32_t gpio, bool on)
{
    return gpio_output_set_level(gpio, on == output->on_level);
}

static int32_t curtain_gpio(const output_t *output, uint32_t direction)
{
    return (direction == (uint32_t)ACTUATOR_CURTAIN_UP) ? output->gpio : output->gpio_down;
}

/* Both relays off, whatever happens. Used while another call already failed, so a second failure
 * adds nothing to report. */
static void curtain_all_off(const output_t *output)
{
    (void)set_relay(output, output->gpio, false);
    (void)set_relay(output, output->gpio_down, false);
}

/* Releases the running direction, waits, then engages the new one: never both on. */
static esp_err_t move_curtain(const output_t *output, uint32_t previous, uint32_t wanted)
{
    if (previous != (uint32_t)ACTUATOR_CURTAIN_STOP) {
        ESP_RETURN_ON_ERROR(set_relay(output, curtain_gpio(output, previous), false), TAG,
                            "Curtain not released");
        if (wanted != (uint32_t)ACTUATOR_CURTAIN_STOP) {
            vTaskDelay(pdMS_TO_TICKS(ACTUATOR_CURTAIN_REVERSE_DELAY_MS));
        }
    }
    if (wanted == (uint32_t)ACTUATOR_CURTAIN_STOP) {
        return ESP_OK;
    }
    return set_relay(output, curtain_gpio(output, wanted), true);
}

static esp_err_t drive_curtain(const output_t *output, uint32_t previous, uint32_t wanted)
{
    const esp_err_t err = move_curtain(output, previous, wanted);
    if (err != ESP_OK) {
        curtain_all_off(output);
    }
    return err;
}

/* Brings `output` from `previous` to `state` (0/1 for a switch, actuator_curtain_t for a curtain);
 * an RGB LED shows `color` (0xRRGGBB) while on. */
static esp_err_t drive(const output_t *output, uint32_t previous, uint32_t state, uint32_t color)
{
    const uint32_t shown = (state != 0U) ? color : 0U;
    switch (output->kind) {
    case OUTPUT_RGB_LED:
        return rgb_led_set(channel(shown, ACTUATOR_COLOR_RED_SHIFT),
                           channel(shown, ACTUATOR_COLOR_GREEN_SHIFT), channel(shown, 0U));
    case OUTPUT_GPIO:
        return set_relay(output, output->gpio, state != 0U);
    case OUTPUT_CURTAIN:
        return drive_curtain(output, previous, state);
    case OUTPUT_NONE:
        return ESP_OK;
    default:
        return ESP_ERR_INVALID_STATE; /* Unreachable: every kind is handled above. */
    }
}

/* Configures one pad already at its off level, so a relay never sees a pulse. */
static esp_err_t init_pad(const output_t *output, int32_t gpio, const char *id)
{
    ESP_RETURN_ON_ERROR(gpio_output_init(gpio, output->pull_up, !output->on_level), TAG,
                        "%s output on GPIO%d not initialized", id, (int)gpio);
    return ESP_OK;
}

static esp_err_t init_outputs(void)
{
    ESP_RETURN_ON_ERROR(rgb_led_init(), TAG, "RGB LED not initialized");
    for (uint32_t i = 0U; i < (uint32_t)ACTUATOR_COUNT; i++) {
        const output_t *output = &s_actuators[i].output;
        const char *id = s_actuators[i].info.id;
        if ((output->kind == OUTPUT_GPIO) || (output->kind == OUTPUT_CURTAIN)) {
            ESP_RETURN_ON_ERROR(init_pad(output, output->gpio, id), TAG, "Output not initialized");
        }
        if (output->kind == OUTPUT_CURTAIN) {
            ESP_RETURN_ON_ERROR(init_pad(output, output->gpio_down, id), TAG,
                                "Output not initialized");
        }
    }
    return ESP_OK;
}

/* Switches the pad off and gives it back (an idle pad is an input with the reset pull). */
static void release_pad(const output_t *output, int32_t gpio)
{
    (void)set_relay(output, gpio,
                    false); /* Best effort on the way out: the pad is released next. */
    gpio_output_deinit(gpio);
}

/* ---- State ---- */

static bool is_curtain(actuator_t actuator)
{
    return s_actuators[actuator].info.kind == ACTUATOR_KIND_CURTAIN;
}

static bool is_rgb(actuator_t actuator)
{
    return s_actuators[actuator].info.kind == ACTUATOR_KIND_RGB;
}

/* What the output should do: the command while enabled, off (or stop) otherwise. */
static uint32_t wanted_state(actuator_t actuator)
{
    return atomic_load(&s_enabled[actuator]) ? (uint32_t)atomic_load(&s_command[actuator]) : 0U;
}

/* "ON"/"OFF" for a switch, "up"/"down"/"stop" for a curtain. */
static const char *state_text(actuator_t actuator, uint32_t state)
{
    if (is_curtain(actuator)) {
        return s_curtain_text[state];
    }
    return (state != 0U) ? "ON" : "OFF";
}

static void log_state(actuator_t actuator, uint32_t state)
{
    const char *id = s_actuators[actuator].info.id;
    if (is_rgb(actuator)) {
        ESP_LOGI(TAG, "%s -> %s (#%06" PRIx32 ")", id, state_text(actuator, state),
                 (uint32_t)atomic_load(&s_output_color[actuator]));
    } else {
        ESP_LOGI(TAG, "%s -> %s", id, state_text(actuator, state));
    }
}

/* A failing curtain was switched off as a whole; any other output keeps what it had. */
static void keep_state_after_error(actuator_t actuator)
{
    if (is_curtain(actuator)) {
        atomic_store(&s_output_state[actuator], (uint32_t)ACTUATOR_CURTAIN_STOP);
    }
}

/* Brings the output in line with enabled && command (and the colour, while on). On a driver error
 * the output keeps its previous state, and so does the published one. */
static void apply_output(actuator_t actuator)
{
    const uint32_t state = wanted_state(actuator);
    const uint32_t previous = (uint32_t)atomic_load(&s_output_state[actuator]);
    const uint32_t color = (uint32_t)atomic_load(&s_color[actuator]);
    const bool same_color = (state == 0U) || !is_rgb(actuator) ||
                            (color == (uint32_t)atomic_load(&s_output_color[actuator]));
    if ((state == previous) && same_color) {
        return; /* Nothing to change; an off output shows no colour. */
    }
    const esp_err_t err = drive(&s_actuators[actuator].output, previous, state, color);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s not switched %s (%s)", s_actuators[actuator].info.id,
                 state_text(actuator, state), esp_err_to_name(err));
        keep_state_after_error(actuator);
        return;
    }
    atomic_store(&s_output_state[actuator], state);
    if (state != 0U) {
        atomic_store(&s_output_color[actuator], color);
    }
    log_state(actuator, state);
}

/* "#rrggbb" (either case) to 0xRRGGBB. */
static bool parse_color(const char *text, uint32_t *out_color)
{
    if ((strlen(text) != ACTUATOR_COLOR_TEXT_LEN) || (text[0] != '#')) {
        return false;
    }
    for (size_t i = 1U; i < ACTUATOR_COLOR_TEXT_LEN; i++) {
        if (isxdigit((unsigned char)text[i]) == 0) {
            return false;
        }
    }
    *out_color = (uint32_t)strtoul(&text[1], NULL, ACTUATOR_COLOR_HEX_BASE);
    return true;
}

/* "up", "down" or "stop" to its actuator_curtain_t. */
static bool parse_curtain(const char *text, uint32_t *out_direction)
{
    for (uint32_t i = 0U; i < (uint32_t)CURTAIN_STATE_COUNT; i++) {
        if (strcmp(text, s_curtain_text[i]) == 0) {
            *out_direction = i;
            return true;
        }
    }
    return false;
}

/* ---- Public API ---- */

esp_err_t actuator_controller_init(void)
{
    assert(!s_initialized);

    s_initialized = true;
    for (uint32_t i = 0U; i < (uint32_t)ACTUATOR_COUNT; i++) {
        atomic_store(&s_enabled[i], false);
        atomic_store(&s_command[i], 0U);
        atomic_store(&s_color[i], ACTUATOR_DEFAULT_COLOR);
        atomic_store(&s_output_state[i], 0U);
        atomic_store(&s_output_color[i], ACTUATOR_DEFAULT_COLOR);
    }
    const esp_err_t err = init_outputs();
    if (err != ESP_OK) {
        actuator_controller_deinit();
    }
    return err;
}

void actuator_controller_deinit(void)
{
    for (uint32_t i = 0U; i < (uint32_t)ACTUATOR_COUNT; i++) {
        const output_t *output = &s_actuators[i].output;
        if ((output->kind == OUTPUT_GPIO) || (output->kind == OUTPUT_CURTAIN)) {
            release_pad(output, output->gpio);
        }
        if (output->kind == OUTPUT_CURTAIN) {
            release_pad(output, output->gpio_down);
        }
        atomic_store(&s_output_state[i], 0U);
    }
    rgb_led_deinit(); /* Switches the LED off. */
    s_initialized = false;
}

const actuator_info_t *actuator_controller_info(actuator_t actuator)
{
    assert(actuator < ACTUATOR_COUNT);
    return &s_actuators[actuator].info;
}

void actuator_controller_set_enabled(actuator_t actuator, bool enabled)
{
    assert(s_initialized);
    assert(actuator < ACTUATOR_COUNT);
    if (atomic_exchange(&s_enabled[actuator], enabled) != enabled) {
        ESP_LOGI(TAG, "%s %s", s_actuators[actuator].info.id, enabled ? "enabled" : "disabled");
    }
    apply_output(actuator);
}

void actuator_controller_set_command(actuator_t actuator, bool on)
{
    assert(s_initialized);
    assert((actuator < ACTUATOR_COUNT) && !is_curtain(actuator));
    atomic_store(&s_command[actuator], on ? 1U : 0U);
    apply_output(actuator);
}

void actuator_controller_set_curtain(actuator_t actuator, const char *command)
{
    assert(s_initialized);
    assert((actuator < ACTUATOR_COUNT) && is_curtain(actuator));
    assert(command != NULL);

    uint32_t direction = (uint32_t)ACTUATOR_CURTAIN_STOP;
    if ((command[0] != '\0') && !parse_curtain(command, &direction)) {
        ESP_LOGW(TAG, "%s: invalid command '%s', stopping", s_actuators[actuator].info.id, command);
    }
    atomic_store(&s_command[actuator], direction);
    apply_output(actuator);
}

actuator_curtain_t actuator_controller_get_curtain(actuator_t actuator)
{
    assert((actuator < ACTUATOR_COUNT) && is_curtain(actuator));
    return (actuator_curtain_t)atomic_load(&s_output_state[actuator]);
}

void actuator_controller_set_color(actuator_t actuator, const char *color)
{
    assert(s_initialized);
    assert((actuator < ACTUATOR_COUNT) && is_rgb(actuator));
    assert(color != NULL);

    uint32_t parsed = ACTUATOR_DEFAULT_COLOR;
    if ((color[0] != '\0') && !parse_color(color, &parsed)) {
        ESP_LOGW(TAG, "%s: invalid colour '%s', using the default", s_actuators[actuator].info.id,
                 color);
    }
    atomic_store(&s_color[actuator], parsed);
    apply_output(actuator);
}

uint32_t actuator_controller_get_color(actuator_t actuator)
{
    assert((actuator < ACTUATOR_COUNT) && is_rgb(actuator));
    return (uint32_t)atomic_load(&s_output_color[actuator]);
}

bool actuator_controller_is_enabled(actuator_t actuator)
{
    assert(actuator < ACTUATOR_COUNT);
    return atomic_load(&s_enabled[actuator]);
}

bool actuator_controller_is_on(actuator_t actuator)
{
    assert(actuator < ACTUATOR_COUNT);
    return atomic_load(&s_output_state[actuator]) != 0U;
}

/* Packs the real state of one actuator (see ACTUATOR_SNAPSHOT_*). */
static uint32_t pack_state(actuator_t actuator)
{
    if (!actuator_controller_is_enabled(actuator)) {
        return 0U;
    }
    uint32_t state = actuator_controller_is_on(actuator) ? 1U : 0U;
    switch (s_actuators[actuator].info.kind) {
    case ACTUATOR_KIND_RGB:
        state = (state != 0U) ? ACTUATOR_SNAPSHOT_RGB_ON_BIT : 0U;
        state |= actuator_controller_get_color(actuator) & ACTUATOR_SNAPSHOT_COLOR_MASK;
        break;
    case ACTUATOR_KIND_CURTAIN:
        state = (uint32_t)actuator_controller_get_curtain(actuator);
        break;
    case ACTUATOR_KIND_SWITCH:
    default:
        break;
    }
    return state | ACTUATOR_SNAPSHOT_ENABLED_BIT;
}

void actuator_controller_snapshot(actuator_snapshot_t *out)
{
    assert(out != NULL);
    for (uint32_t i = 0U; i < (uint32_t)ACTUATOR_COUNT; i++) {
        out->state[i] = pack_state((actuator_t)i);
    }
}

/* Adds one enabled actuator from its packed state. */
static void add_state(json_writer_t *writer, actuator_t actuator, uint32_t state)
{
    const char *id = s_actuators[actuator].info.id;
    switch (s_actuators[actuator].info.kind) {
    case ACTUATOR_KIND_RGB:
        json_writer_add(writer, "\"%s\":{\"on\":%s,\"color\":\"#%06" PRIx32 "\"}", id,
                        utils_bool_text((state & ACTUATOR_SNAPSHOT_RGB_ON_BIT) != 0U),
                        state & ACTUATOR_SNAPSHOT_COLOR_MASK);
        break;
    case ACTUATOR_KIND_CURTAIN: {
        const uint32_t direction = state & ~ACTUATOR_SNAPSHOT_ENABLED_BIT;
        json_writer_add(writer, "\"%s\":\"%s\"", id,
                        s_curtain_text[(direction < CURTAIN_STATE_COUNT) ? direction : 0U]);
        break;
    }
    case ACTUATOR_KIND_SWITCH:
    default:
        json_writer_add(writer, "\"%s\":%s", id,
                        utils_bool_text((state & ~ACTUATOR_SNAPSHOT_ENABLED_BIT) != 0U));
        break;
    }
}

esp_err_t actuator_snapshot_to_json(const actuator_snapshot_t *snapshot, char *buffer, size_t size)
{
    assert(snapshot != NULL);
    json_writer_t writer;
    json_writer_begin(&writer, buffer, size);
    for (uint32_t i = 0U; i < (uint32_t)ACTUATOR_COUNT; i++) {
        if ((snapshot->state[i] & ACTUATOR_SNAPSHOT_ENABLED_BIT) != 0U) { /* Disabled: left out. */
            add_state(&writer, (actuator_t)i, snapshot->state[i]);
        }
    }
    return json_writer_end(&writer);
}

esp_err_t actuator_controller_states_to_json(char *buffer, size_t size)
{
    actuator_snapshot_t snapshot = {0};
    actuator_controller_snapshot(&snapshot);
    return actuator_snapshot_to_json(&snapshot, buffer, size);
}

esp_err_t actuator_controller_catalog_to_json(char *buffer, size_t size)
{
    json_writer_t writer;
    json_writer_begin(&writer, buffer, size);
    for (uint32_t i = 0U; i < (uint32_t)ACTUATOR_COUNT; i++) {
        const actuator_info_t *info = &s_actuators[i].info;
        const json_catalog_item_t item = {info->id, info->type, NULL, info->simulated};
        json_writer_add_catalog_item(&writer, &item);
    }
    return json_writer_end(&writer);
}
