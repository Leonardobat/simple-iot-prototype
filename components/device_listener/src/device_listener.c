#include "device_listener.h"

#include <assert.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "actuator_controller.h"
#include "backoff.h"
#include "device_listener_config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "firebase_event.h"
#include "firebase_paths.h"
#include "firebase_rtdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "json_parser.h"
#include "sensor_provider.h"
#include "utils.h"

static const char *TAG = "device_listener";

/* A boolean field of one catalog item: /<collection>/<id>/<field>. */
typedef struct {
    const char *collection;
    const char *id;
    const char *field;
} field_ref_t;

static TaskHandle_t s_task;
static SemaphoreHandle_t s_task_exited;
static atomic_bool s_stop;
/* Used only by the stream task. */
static bool s_event_received;
static json_tok_t s_json_tokens[DEVICE_LISTENER_JSON_MAX_TOKENS];

/* ---- Applying events ---- */

/* Writes "/<collection>/<id>/<field>" into `path` (DEVICE_LISTENER_PATH_MAX_LEN bytes). */
static bool build_field_path(const field_ref_t *field, char *path)
{
    const esp_err_t err = utils_snprintf(path, DEVICE_LISTENER_PATH_MAX_LEN, "/%s/%s/%s",
                                         field->collection, field->id, field->field);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Field path of %s too long", field->id);
    }
    return err == ESP_OK;
}

static bool event_sets(const firebase_event_t *event, const field_ref_t *field, bool *out_value)
{
    char path[DEVICE_LISTENER_PATH_MAX_LEN] = {0};
    return build_field_path(field, path) && firebase_event_bool_at(event, path, out_value);
}

static bool event_sets_text(const firebase_event_t *event, const field_ref_t *field, char *out,
                            size_t size)
{
    char path[DEVICE_LISTENER_PATH_MAX_LEN] = {0};
    return build_field_path(field, path) && firebase_event_string_at(event, path, out, size);
}

static void apply_sensors(const firebase_event_t *event)
{
    for (uint32_t i = 0U; i < (uint32_t)SENSOR_COUNT; i++) {
        const sensor_t sensor = (sensor_t)i;
        const field_ref_t enabled = {DEVICE_LISTENER_SENSORS_KEY, sensor_provider_info(sensor)->id,
                                     DEVICE_LISTENER_ENABLED_KEY};
        bool value = false;
        if (event_sets(event, &enabled, &value)) {
            sensor_provider_set_enabled(sensor, value);
        }
    }
}

/* "command" is true/false for a switch and "up"/"down"/"stop" for a curtain. */
static void apply_command(const firebase_event_t *event, actuator_t actuator)
{
    const actuator_info_t *info = actuator_controller_info(actuator);
    const field_ref_t command = {DEVICE_LISTENER_ACTUATORS_KEY, info->id,
                                 DEVICE_LISTENER_COMMAND_KEY};
    if (info->kind == ACTUATOR_KIND_CURTAIN) {
        char text[DEVICE_LISTENER_TEXT_MAX_LEN] = {0};
        if (event_sets_text(event, &command, text, sizeof(text))) {
            actuator_controller_set_curtain(actuator, text);
        }
        return;
    }
    bool value = false;
    if (event_sets(event, &command, &value)) {
        actuator_controller_set_command(actuator, value);
    }
}

static void apply_color(const firebase_event_t *event, actuator_t actuator)
{
    const actuator_info_t *info = actuator_controller_info(actuator);
    const field_ref_t color = {DEVICE_LISTENER_ACTUATORS_KEY, info->id, DEVICE_LISTENER_COLOR_KEY};
    char text[DEVICE_LISTENER_TEXT_MAX_LEN] = {0};
    if ((info->kind == ACTUATOR_KIND_RGB) && event_sets_text(event, &color, text, sizeof(text))) {
        actuator_controller_set_color(actuator, text);
    }
}

static void apply_actuator(const firebase_event_t *event, actuator_t actuator)
{
    const actuator_info_t *info = actuator_controller_info(actuator);
    const field_ref_t enabled = {DEVICE_LISTENER_ACTUATORS_KEY, info->id,
                                 DEVICE_LISTENER_ENABLED_KEY};
    bool value = false;
    if (event_sets(event, &enabled, &value)) {
        actuator_controller_set_enabled(actuator, value);
    }
    apply_command(event, actuator);
    apply_color(event, actuator);
}

static void apply_actuators(const firebase_event_t *event)
{
    for (uint32_t i = 0U; i < (uint32_t)ACTUATOR_COUNT; i++) {
        apply_actuator(event, (actuator_t)i);
    }
}

static void dispatch(const char *data, bool is_patch)
{
    jparse_ctx_t json = {0};
    if (json_parse_start_static(&json, data, (int)strlen(data), s_json_tokens,
                                DEVICE_LISTENER_JSON_MAX_TOKENS) != OS_SUCCESS) {
        ESP_LOGE(TAG, "Invalid or too large JSON: %s", data);
        return;
    }
    char path[DEVICE_LISTENER_PATH_MAX_LEN] = {0};
    if (json_obj_get_string(&json, "path", path, (int)sizeof(path)) == OS_SUCCESS) {
        const firebase_event_t event = {.json = &json, .path = path, .is_patch = is_patch};
        apply_sensors(&event);
        apply_actuators(&event);
    } else {
        ESP_LOGE(TAG, "Event without a path that fits: %s", data);
    }
    (void)json_parse_end_static(&json); /* Only resets the context; cannot fail. */
}

static void on_stream_event(const char *event, const char *data, void *ctx)
{
    (void)ctx; /* No context: the listener state is static. */
    s_event_received = true;
    if (strcmp(event, "put") == 0) {
        dispatch(data, false);
    } else if (strcmp(event, "patch") == 0) {
        dispatch(data, true);
    } else if (strcmp(event, "keep-alive") != 0) {
        /* cancel / auth_revoked: Firebase closes the stream right after. */
        ESP_LOGW(TAG, "Stream event '%s': %s", event, data);
    } else {
        /* keep-alive: nothing to do. */
    }
}

/* ---- Stream task ---- */

/* Waits before reconnecting; device_listener_deinit() wakes it up early. */
static uint32_t wait_before_retry(uint32_t previous_delay_ms, esp_err_t err)
{
    const uint32_t delay_ms = backoff_next_delay_ms(
        previous_delay_ms, DEVICE_LISTENER_RETRY_INITIAL_MS, DEVICE_LISTENER_RETRY_MAX_MS);
    ESP_LOGW(TAG, "Stream closed (%s), reconnecting in %" PRIu32 " ms", esp_err_to_name(err),
             delay_ms);
    ESP_LOGD(TAG, "Stack high water mark: %u bytes", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delay_ms)); /* Returns early only on deinit. */
    return delay_ms;
}

static void stream_task(void *arg)
{
    (void)arg; /* No argument: the listener state is static. */
    uint32_t delay_ms = 0U;
    while (!atomic_load(&s_stop)) {
        s_event_received = false;
        const esp_err_t err = firebase_rtdb_stream(FIREBASE_DEVICE_PATH, on_stream_event, NULL);
        if (s_event_received) {
            delay_ms = 0U; /* The stream worked: restart the backoff. */
        }
        if (!atomic_load(&s_stop)) {
            delay_ms = wait_before_retry(delay_ms, err);
        }
    }
    (void)xSemaphoreGive(s_task_exited); /* Cannot fail: binary semaphore given once. */
    vTaskDelete(NULL);
}

esp_err_t device_listener_init(void)
{
    assert(s_task == NULL);

    atomic_store(&s_stop, false);
    s_task_exited = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_task_exited != NULL, ESP_ERR_NO_MEM, TAG, "Semaphore alloc failed");

    if (xTaskCreate(stream_task, "device_stream", DEVICE_LISTENER_TASK_STACK_SIZE, NULL,
                    DEVICE_LISTENER_TASK_PRIORITY, &s_task) != pdPASS) {
        vSemaphoreDelete(s_task_exited);
        s_task_exited = NULL;
        s_task = NULL;
        ESP_LOGE(TAG, "Task creation failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t device_listener_deinit(void)
{
    if (s_task == NULL) {
        return ESP_OK;
    }
    atomic_store(&s_stop, true);
    firebase_rtdb_stream_cancel();
    (void)xTaskNotifyGive(s_task); /* Always pdPASS: wakes the backoff wait. */

    ESP_RETURN_ON_FALSE(
        xSemaphoreTake(s_task_exited, pdMS_TO_TICKS(DEVICE_LISTENER_STOP_TIMEOUT_MS)) == pdTRUE,
        ESP_ERR_TIMEOUT, TAG, "Stream task did not stop");
    vSemaphoreDelete(s_task_exited);
    s_task_exited = NULL;
    s_task = NULL;
    return ESP_OK;
}
