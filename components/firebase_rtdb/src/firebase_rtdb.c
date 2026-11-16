#include "firebase_rtdb.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_check.h"
#include "firebase_auth.h"
#include "firebase_rtdb_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "http_client.h"
#include "utils.h"

_Static_assert(FIREBASE_RTDB_URL_BASE_MAX_LEN + sizeof(FIREBASE_RTDB_AUTH_QUERY) +
                       FIREBASE_ID_TOKEN_MAX_LEN <=
                   HTTP_CLIENT_URL_MAX_LEN,
               "An authenticated URL must fit in the HTTP client");

static const char *TAG = "firebase_rtdb";

static bool s_initialized;
static SemaphoreHandle_t s_write_lock;  /* Guards s_write_url (PUT and PATCH). */
static SemaphoreHandle_t s_stream_lock; /* Guards s_stream_url; held while a stream is open. */
static char s_write_url[HTTP_CLIENT_URL_MAX_LEN];
static char s_stream_url[HTTP_CLIENT_URL_MAX_LEN];

/* Writes "<DB_URL>/<path>.json[?auth=<ID token>]" into `url` (HTTP_CLIENT_URL_MAX_LEN bytes). */
static esp_err_t build_url(const char *path, char *url)
{
    ESP_RETURN_ON_ERROR(utils_snprintf(url, FIREBASE_RTDB_URL_BASE_MAX_LEN, "%s/%s.json",
                                       CONFIG_FIREBASE_DB_URL, path),
                        TAG, "Path too long: %s", path);
    if (!firebase_auth_is_enabled()) {
        return ESP_OK;
    }
    const size_t base_len = strlcat(url, FIREBASE_RTDB_AUTH_QUERY, HTTP_CLIENT_URL_MAX_LEN);
    return firebase_auth_get_token(&url[base_len], HTTP_CLIENT_URL_MAX_LEN - base_len);
}

static esp_err_t create_locks(void)
{
    s_write_lock = xSemaphoreCreateMutex();
    s_stream_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE((s_write_lock != NULL) && (s_stream_lock != NULL), ESP_ERR_NO_MEM, TAG,
                        "Mutex alloc failed");
    return ESP_OK;
}

static void delete_lock(SemaphoreHandle_t *lock)
{
    if (*lock != NULL) {
        vSemaphoreDelete(*lock);
        *lock = NULL;
    }
}

esp_err_t firebase_rtdb_init(void)
{
    assert(!s_initialized);

    s_initialized = true;
    esp_err_t err = http_client_init();
    if (err == ESP_OK) {
        err = firebase_auth_init();
    }
    if (err == ESP_OK) {
        err = create_locks();
    }
    if (err != ESP_OK) {
        firebase_rtdb_deinit();
    }
    return err;
}

void firebase_rtdb_deinit(void)
{
    delete_lock(&s_stream_lock);
    delete_lock(&s_write_lock);
    firebase_auth_deinit();
    http_client_deinit();
    s_initialized = false;
}

/* http_client_put() or http_client_patch(). */
typedef esp_err_t (*http_write_t)(const char *url, const char *json_body);

static esp_err_t write_node(http_write_t http_write, const char *path, const char *json_body)
{
    assert(s_initialized);
    assert((path != NULL) && (json_body != NULL));
    ESP_RETURN_ON_FALSE(
        xSemaphoreTake(s_write_lock, pdMS_TO_TICKS(FIREBASE_RTDB_LOCK_TIMEOUT_MS)) == pdTRUE,
        ESP_ERR_TIMEOUT, TAG, "Write buffer busy");

    esp_err_t err = build_url(path, s_write_url);
    if (err == ESP_OK) {
        err = http_write(s_write_url, json_body);
    }
    (void)xSemaphoreGive(s_write_lock); /* Cannot fail: taken above by this task. */
    return err;
}

esp_err_t firebase_rtdb_put(const char *path, const char *json_body)
{
    return write_node(http_client_put, path, json_body);
}

esp_err_t firebase_rtdb_patch(const char *path, const char *json_body)
{
    return write_node(http_client_patch, path, json_body);
}

esp_err_t firebase_rtdb_stream(const char *path, sse_parser_event_handler_t on_event, void *ctx)
{
    assert(s_initialized);
    assert((path != NULL) && (on_event != NULL));
    ESP_RETURN_ON_FALSE(xSemaphoreTake(s_stream_lock, 0) == pdTRUE, ESP_ERR_INVALID_STATE, TAG,
                        "A stream is already open");

    esp_err_t err = build_url(path, s_stream_url);
    if (err == ESP_OK) {
        err = http_client_stream(s_stream_url, on_event, ctx);
    }
    (void)xSemaphoreGive(s_stream_lock); /* Cannot fail: taken above by this task. */
    return err;
}

void firebase_rtdb_stream_cancel(void)
{
    http_client_stream_cancel();
}
