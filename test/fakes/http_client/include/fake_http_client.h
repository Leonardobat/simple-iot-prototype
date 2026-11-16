/**
 * @file fake_http_client.h
 * @brief Test double of http_client: records calls, returns scripted results, no network.
 */
#ifndef FAKE_HTTP_CLIENT_H
#define FAKE_HTTP_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "http_client.h"

#define FAKE_HTTP_BODY_MAX_LEN 1024U

/** One scripted Server-Sent Event. */
typedef struct {
    const char *event;
    const char *data;
} fake_sse_event_t;

typedef struct {
    uint32_t init_calls;
    uint32_t deinit_calls;
    uint32_t put_calls;
    uint32_t patch_calls;
    uint32_t post_calls;
    uint32_t stream_calls;
    uint32_t cancel_calls;
    char last_url[HTTP_CLIENT_URL_MAX_LEN]; /**< URL of the latest put/patch/post/stream call. */
    char last_body[FAKE_HTTP_BODY_MAX_LEN]; /**< Body of the latest put/patch/post call. */
    esp_err_t put_result;                   /**< Returned by http_client_put(). */
    esp_err_t patch_result;                 /**< Returned by http_client_patch(). */
    esp_err_t post_result;                  /**< Returned by http_client_post(). */
    esp_err_t stream_result;                /**< Returned by a non-blocking http_client_stream(). */
    const char *post_response;              /**< Copied into the caller's buffer by post. */
    uint32_t post_delay_ms;                 /**< Simulated network latency of post. */
    bool stream_blocks; /**< true: the stream stays open, delivering fake_http_client_emit() events
                             until http_client_stream_cancel(), then returns ESP_OK. */
} fake_http_client_t;

extern fake_http_client_t fake_http_client;

/** Clears every counter and pending cancel/event; results default to ESP_OK. */
void fake_http_client_reset(void);

/**
 * @brief Delivers @p events through the blocking stream open in another task.
 * @return false if no stream handled them within 1 s.
 */
bool fake_http_client_emit(const fake_sse_event_t *events, size_t count);

#endif /* FAKE_HTTP_CLIENT_H */
