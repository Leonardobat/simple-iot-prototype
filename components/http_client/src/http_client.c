#include "http_client.h"

#include <assert.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "http_client_config.h"

typedef struct {
    esp_http_client_handle_t handle;
    SemaphoreHandle_t lock;
} client_slot_t;

static const char *TAG = "http_client";

static bool s_initialized;
static client_slot_t s_write; /* PUT and PATCH share one keep-alive connection. */
static client_slot_t s_post;
static client_slot_t s_stream;
static atomic_bool s_stream_cancel;
/* Only the task holding s_stream.lock uses it. */
static char s_stream_buffer[HTTP_CLIENT_STREAM_READ_BUFFER_SIZE];

/* ---- Helpers ---- */

/* Length of `url` up to the query string, which may hold tokens or API keys. */
static int loggable_len(const char *url)
{
    return (int)strcspn(url, "?");
}

static esp_err_t validate_url(const char *url)
{
    assert(s_initialized);
    assert(url != NULL);
    ESP_RETURN_ON_FALSE(strlen(url) < HTTP_CLIENT_URL_MAX_LEN, ESP_ERR_INVALID_SIZE, TAG,
                        "URL longer than %u bytes", (unsigned)HTTP_CLIENT_URL_MAX_LEN);
    return ESP_OK;
}

static esp_err_t lock_slot(client_slot_t *slot, TickType_t timeout)
{
    return (xSemaphoreTake(slot->lock, timeout) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void unlock_slot(client_slot_t *slot)
{
    /* Cannot fail: the mutex was taken by this task. */
    (void)xSemaphoreGive(slot->lock);
}

static void close_connection(esp_http_client_handle_t handle)
{
    if (esp_http_client_close(handle) != ESP_OK) {
        ESP_LOGW(TAG, "Closing the connection failed");
    }
}

static esp_err_t check_status(esp_http_client_handle_t handle, const char *url)
{
    const int32_t status = (int32_t)esp_http_client_get_status_code(handle);
    if ((status >= 200) && (status < 300)) {
        return ESP_OK;
    }
    ESP_LOGE(TAG, "%.*s -> HTTP %" PRId32, loggable_len(url), url, status);
    return ESP_FAIL;
}

/* ---- Init / deinit ---- */

static esp_err_t slot_init(client_slot_t *slot, esp_http_client_method_t method,
                           const char *header_name, const char *header_value)
{
    slot->lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(slot->lock != NULL, ESP_ERR_NO_MEM, TAG, "Mutex alloc failed");

    const esp_http_client_config_t config = {
        .url = HTTP_CLIENT_PLACEHOLDER_URL,
        .method = method,
        .timeout_ms = HTTP_CLIENT_REQUEST_TIMEOUT_MS,
        .buffer_size_tx = (int)HTTP_CLIENT_TX_BUFFER_SIZE,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    slot->handle = esp_http_client_init(&config);
    ESP_RETURN_ON_FALSE(slot->handle != NULL, ESP_ERR_NO_MEM, TAG, "Client alloc failed");
    return esp_http_client_set_header(slot->handle, header_name, header_value);
}

static void slot_deinit(client_slot_t *slot)
{
    if ((slot->handle != NULL) && (esp_http_client_cleanup(slot->handle) != ESP_OK)) {
        ESP_LOGW(TAG, "Client cleanup failed");
    }
    if (slot->lock != NULL) {
        vSemaphoreDelete(slot->lock);
    }
    slot->handle = NULL;
    slot->lock = NULL;
}

static esp_err_t init_slots(void)
{
    ESP_RETURN_ON_ERROR(
        slot_init(&s_write, HTTP_METHOD_PUT, "Content-Type", HTTP_CLIENT_CONTENT_TYPE_JSON), TAG,
        "Write client init failed");
    ESP_RETURN_ON_ERROR(
        slot_init(&s_post, HTTP_METHOD_POST, "Content-Type", HTTP_CLIENT_CONTENT_TYPE_JSON), TAG,
        "POST client init failed");
    return slot_init(&s_stream, HTTP_METHOD_GET, "Accept", HTTP_CLIENT_ACCEPT_SSE);
}

esp_err_t http_client_init(void)
{
    assert(!s_initialized);

    s_initialized = true;
    atomic_store(&s_stream_cancel, false);
    const esp_err_t err = init_slots();
    if (err != ESP_OK) {
        http_client_deinit();
    }
    return err;
}

void http_client_deinit(void)
{
    slot_deinit(&s_stream);
    slot_deinit(&s_post);
    slot_deinit(&s_write);
    s_initialized = false;
}

/* ---- PUT / PATCH (shared keep-alive connection) ---- */

static esp_err_t perform_write(esp_http_client_method_t method, const char *url,
                               const char *json_body)
{
    ESP_RETURN_ON_ERROR(esp_http_client_set_url(s_write.handle, url), TAG, "Invalid URL");
    ESP_RETURN_ON_ERROR(esp_http_client_set_method(s_write.handle, method), TAG, "Method not set");
    ESP_RETURN_ON_ERROR(
        esp_http_client_set_post_field(s_write.handle, json_body, (int)strlen(json_body)), TAG,
        "Body not set");
    return esp_http_client_perform(s_write.handle);
}

/* The server may have dropped the idle keep-alive connection: reconnect and retry once. */
static esp_err_t perform_write_with_retry(esp_http_client_method_t method, const char *url,
                                          const char *json_body)
{
    const esp_err_t err = perform_write(method, url, json_body);
    if (err == ESP_OK) {
        return check_status(s_write.handle, url);
    }
    ESP_LOGW(TAG, "Request failed (%s), reconnecting", esp_err_to_name(err));
    close_connection(s_write.handle);
    ESP_RETURN_ON_ERROR(perform_write(method, url, json_body), TAG, "Retry failed");
    return check_status(s_write.handle, url);
}

static esp_err_t write_json(esp_http_client_method_t method, const char *url, const char *json_body)
{
    assert(json_body != NULL);
    ESP_RETURN_ON_ERROR(validate_url(url), TAG, "Invalid write URL");
    ESP_RETURN_ON_ERROR(lock_slot(&s_write, pdMS_TO_TICKS(HTTP_CLIENT_LOCK_TIMEOUT_MS)), TAG,
                        "Write client busy");

    const esp_err_t err = perform_write_with_retry(method, url, json_body);
    unlock_slot(&s_write);
    return err;
}

esp_err_t http_client_put(const char *url, const char *json_body)
{
    return write_json(HTTP_METHOD_PUT, url, json_body);
}

esp_err_t http_client_patch(const char *url, const char *json_body)
{
    return write_json(HTTP_METHOD_PATCH, url, json_body);
}

/* ---- POST (one connection per request) ---- */

static esp_err_t send_body(esp_http_client_handle_t handle, const char *json_body)
{
    const int body_len = (int)strlen(json_body);
    ESP_RETURN_ON_ERROR(esp_http_client_open(handle, body_len), TAG, "Connection failed");
    ESP_RETURN_ON_FALSE(esp_http_client_write(handle, json_body, body_len) == body_len, ESP_FAIL,
                        TAG, "Write failed");
    ESP_RETURN_ON_FALSE(esp_http_client_fetch_headers(handle) >= 0, ESP_FAIL, TAG,
                        "No response headers");
    return ESP_OK;
}

static esp_err_t read_body(esp_http_client_handle_t handle, char *response, size_t size)
{
    const int len = esp_http_client_read_response(handle, response, (int)(size - 1U));
    ESP_RETURN_ON_FALSE(len >= 0, ESP_FAIL, TAG, "Read failed");
    response[len] = '\0';
    return ESP_OK;
}

static esp_err_t exchange(const char *url, const char *json_body, char *response, size_t size)
{
    ESP_RETURN_ON_ERROR(esp_http_client_set_url(s_post.handle, url), TAG, "Invalid URL");
    esp_err_t err = send_body(s_post.handle, json_body);
    if (err == ESP_OK) {
        err = read_body(s_post.handle, response, size);
    }
    if (err == ESP_OK) {
        err = check_status(s_post.handle, url);
    }
    close_connection(s_post.handle);
    return err;
}

esp_err_t http_client_post(const char *url, const char *json_body, char *response, size_t size)
{
    assert((json_body != NULL) && (response != NULL) && (size > 0U));
    response[0] = '\0';
    ESP_RETURN_ON_ERROR(validate_url(url), TAG, "Invalid POST URL");
    ESP_RETURN_ON_ERROR(lock_slot(&s_post, pdMS_TO_TICKS(HTTP_CLIENT_LOCK_TIMEOUT_MS)), TAG,
                        "POST client busy");

    const esp_err_t err = exchange(url, json_body, response, size);
    unlock_slot(&s_post);
    return err;
}

/* ---- Server-Sent Events stream ---- */

static bool is_redirect(int32_t status)
{
    return (status == (int32_t)HttpStatus_MovedPermanently) ||
           (status == (int32_t)HttpStatus_Found) || (status == (int32_t)HttpStatus_SeeOther) ||
           (status == (int32_t)HttpStatus_TemporaryRedirect) ||
           (status == (int32_t)HttpStatus_PermanentRedirect);
}

/* Opens the connection and reads the headers. Leaves it open unless an error is returned. */
static esp_err_t open_once(esp_http_client_handle_t handle, int32_t *out_status)
{
    ESP_RETURN_ON_ERROR(esp_http_client_open(handle, 0), TAG, "Connection failed");
    if (esp_http_client_fetch_headers(handle) < 0) {
        close_connection(handle);
        ESP_LOGE(TAG, "No response headers");
        return ESP_FAIL;
    }
    *out_status = (int32_t)esp_http_client_get_status_code(handle);
    return ESP_OK;
}

static esp_err_t open_stream(const char *url)
{
    esp_http_client_handle_t handle = s_stream.handle;
    ESP_RETURN_ON_ERROR(esp_http_client_set_url(handle, url), TAG, "Invalid URL");
    ESP_RETURN_ON_ERROR(esp_http_client_reset_redirect_counter(handle), TAG, "Redirect reset");
    ESP_RETURN_ON_ERROR(esp_http_client_set_timeout_ms(handle, HTTP_CLIENT_REQUEST_TIMEOUT_MS), TAG,
                        "Timeout not set");

    for (uint32_t redirects = 0U; redirects <= HTTP_CLIENT_MAX_REDIRECTS; redirects++) {
        int32_t status = 0;
        ESP_RETURN_ON_ERROR(open_once(handle, &status), TAG, "Stream open failed");
        if (!is_redirect(status)) {
            return check_status(handle, url);
        }
        const esp_err_t err = esp_http_client_set_redirection(handle);
        close_connection(handle);
        ESP_RETURN_ON_ERROR(err, TAG, "Invalid redirect");
    }
    ESP_LOGE(TAG, "More than %u redirects", (unsigned)HTTP_CLIENT_MAX_REDIRECTS);
    return ESP_FAIL;
}

/* Feeds the parser until the server closes, the stream goes idle or a cancel is requested. */
static esp_err_t read_stream(sse_parser_handle_t parser)
{
    ESP_RETURN_ON_ERROR(esp_http_client_set_timeout_ms(s_stream.handle, HTTP_CLIENT_STREAM_POLL_MS),
                        TAG, "Timeout not set");
    uint32_t idle_ms = 0U;
    while (!atomic_load(&s_stream_cancel)) {
        const int len =
            esp_http_client_read(s_stream.handle, s_stream_buffer, (int)sizeof(s_stream_buffer));
        if (len == -ESP_ERR_HTTP_EAGAIN) {
            idle_ms += (uint32_t)HTTP_CLIENT_STREAM_POLL_MS;
            ESP_RETURN_ON_FALSE(idle_ms < HTTP_CLIENT_STREAM_IDLE_TIMEOUT_MS, ESP_ERR_TIMEOUT, TAG,
                                "Stream idle for %" PRIu32 " ms", idle_ms);
            continue;
        }
        ESP_RETURN_ON_FALSE(len >= 0, ESP_FAIL, TAG, "Stream read failed");
        if (len == 0) {
            return ESP_OK; /* Closed by the server. */
        }
        idle_ms = 0U;
        sse_parser_feed(parser, s_stream_buffer, (size_t)len);
    }
    return ESP_OK;
}

static esp_err_t run_stream(const char *url, sse_parser_handle_t parser)
{
    ESP_LOGI(TAG, "Opening stream on %.*s", loggable_len(url), url);
    esp_err_t err = open_stream(url);
    if (err == ESP_OK) {
        err = read_stream(parser);
    }
    close_connection(s_stream.handle);
    atomic_store(&s_stream_cancel, false);
    return err;
}

esp_err_t http_client_stream(const char *url, sse_parser_event_handler_t on_event, void *ctx)
{
    assert(on_event != NULL);
    ESP_RETURN_ON_ERROR(validate_url(url), TAG, "Invalid stream URL");
    ESP_RETURN_ON_FALSE(lock_slot(&s_stream, 0) == ESP_OK, ESP_ERR_INVALID_STATE, TAG,
                        "A stream is already open");

    sse_parser_handle_t parser = NULL;
    esp_err_t err = sse_parser_init(on_event, ctx, &parser);
    if (err == ESP_OK) {
        err = run_stream(url, parser);
        sse_parser_deinit(parser);
    }
    unlock_slot(&s_stream);
    return err;
}

void http_client_stream_cancel(void)
{
    atomic_store(&s_stream_cancel, true);
}
