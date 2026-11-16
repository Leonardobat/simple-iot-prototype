#include "fake_http_client.h"

#include <stdatomic.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define EMIT_TIMEOUT_MS 1000

fake_http_client_t fake_http_client;

static SemaphoreHandle_t s_stream_wakeup;
static SemaphoreHandle_t s_events_handled;
static atomic_bool s_cancel;
static const fake_sse_event_t *s_events;
static size_t s_event_count;

static void record(const char *url, const char *body)
{
    (void)strlcpy(fake_http_client.last_url, url, sizeof(fake_http_client.last_url));
    (void)strlcpy(fake_http_client.last_body, (body != NULL) ? body : "",
                  sizeof(fake_http_client.last_body));
}

void fake_http_client_reset(void)
{
    if (s_stream_wakeup == NULL) {
        s_stream_wakeup = xSemaphoreCreateBinary();
        s_events_handled = xSemaphoreCreateBinary();
    }
    (void)xSemaphoreTake(s_stream_wakeup, 0);
    (void)xSemaphoreTake(s_events_handled, 0);
    atomic_store(&s_cancel, false);
    memset(&fake_http_client, 0, sizeof(fake_http_client));
    fake_http_client.put_result = ESP_OK;
    fake_http_client.patch_result = ESP_OK;
    fake_http_client.post_result = ESP_OK;
    fake_http_client.stream_result = ESP_OK;
    fake_http_client.post_response = "";
}

bool fake_http_client_emit(const fake_sse_event_t *events, size_t count)
{
    s_events = events;
    s_event_count = count;
    (void)xSemaphoreGive(s_stream_wakeup);
    return xSemaphoreTake(s_events_handled, pdMS_TO_TICKS(EMIT_TIMEOUT_MS)) == pdTRUE;
}

esp_err_t http_client_init(void)
{
    fake_http_client.init_calls++;
    return ESP_OK;
}

void http_client_deinit(void)
{
    fake_http_client.deinit_calls++;
}

esp_err_t http_client_put(const char *url, const char *json_body)
{
    fake_http_client.put_calls++;
    record(url, json_body);
    return fake_http_client.put_result;
}

esp_err_t http_client_patch(const char *url, const char *json_body)
{
    fake_http_client.patch_calls++;
    record(url, json_body);
    return fake_http_client.patch_result;
}

esp_err_t http_client_post(const char *url, const char *json_body, char *response, size_t size)
{
    fake_http_client.post_calls++;
    record(url, json_body);
    vTaskDelay(pdMS_TO_TICKS(fake_http_client.post_delay_ms));
    (void)strlcpy(response, fake_http_client.post_response, size);
    return fake_http_client.post_result;
}

static void run_open_stream(sse_parser_event_handler_t on_event, void *ctx)
{
    for (;;) {
        (void)xSemaphoreTake(s_stream_wakeup, portMAX_DELAY);
        if (atomic_exchange(&s_cancel, false)) {
            return;
        }
        for (size_t i = 0U; i < s_event_count; i++) {
            on_event(s_events[i].event, s_events[i].data, ctx);
        }
        (void)xSemaphoreGive(s_events_handled);
    }
}

esp_err_t http_client_stream(const char *url, sse_parser_event_handler_t on_event, void *ctx)
{
    fake_http_client.stream_calls++;
    record(url, NULL);
    if (!fake_http_client.stream_blocks) {
        return fake_http_client.stream_result;
    }
    run_open_stream(on_event, ctx);
    return ESP_OK;
}

void http_client_stream_cancel(void)
{
    fake_http_client.cancel_calls++;
    atomic_store(&s_cancel, true);
    (void)xSemaphoreGive(s_stream_wakeup);
}
