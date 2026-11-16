/**
 * @file http_client.h
 * @brief HTTPS transport (HAL over esp_http_client) used by the Firebase components.
 *
 * Takes absolute URLs and knows nothing about Firebase. TLS is verified with the ESP-IDF
 * certificate bundle. Three clients (PUT/PATCH, POST, stream) are created once in
 * http_client_init() and reused, so no request allocates memory in this component. Query strings
 * may carry secrets (tokens, API keys) and are never logged.
 */
#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stddef.h>

#include "esp_err.h"
#include "sse_parser.h"

/** Longest accepted URL, including the terminating NUL. */
#define HTTP_CLIENT_URL_MAX_LEN 2560U

/**
 * @brief Creates the PUT/PATCH, POST and stream clients.
 *
 * @return
 *  - ESP_OK
 *  - ESP_ERR_NO_MEM when a client or mutex cannot be allocated (nothing is left allocated).
 * @pre Not initialized (asserted).
 * @post Every other function may be called until http_client_deinit().
 * @note Not thread-safe: call once at start-up, before other tasks use the component.
 */
esp_err_t http_client_init(void);

/**
 * @brief Releases every client and mutex.
 *
 * @pre No request or stream is in progress: call http_client_stream_cancel() and wait for
 *      http_client_stream() to return first.
 * @post The component can be initialized again.
 * @note Not thread-safe. Safe to call on a partially initialized component.
 */
void http_client_deinit(void);

/**
 * @brief PUTs a JSON body to @p url.
 *
 * PUT and PATCH share one keep-alive connection, so consecutive requests to the same host skip the
 * TLS handshake. A request on a connection dropped by the server is retried once.
 *
 * @param[in] url       Absolute https URL.
 * @param[in] json_body NUL-terminated JSON sent as application/json.
 * @return
 *  - ESP_OK on HTTP 2xx
 *  - ESP_FAIL on any other HTTP status
 *  - ESP_ERR_INVALID_SIZE if @p url is longer than HTTP_CLIENT_URL_MAX_LEN - 1
 *  - ESP_ERR_TIMEOUT if the client stayed busy for HTTP_CLIENT_LOCK_TIMEOUT_MS
 *  - ESP_ERR_HTTP_* transport errors (connect, TLS, write)
 * @pre Initialized; @p url and @p json_body are not NULL (asserted).
 * @note Thread-safe. Blocking: up to 2 x 10 s plus the wait for the client.
 */
esp_err_t http_client_put(const char *url, const char *json_body);

/**
 * @brief PATCHes a JSON body to @p url, on the same connection and with the same retry as
 *        http_client_put().
 *
 * @param[in] url       Absolute https URL.
 * @param[in] json_body NUL-terminated JSON object sent as application/json.
 * @return The same errors as http_client_put().
 * @pre Initialized; @p url and @p json_body are not NULL (asserted).
 * @note Thread-safe. Blocking: up to 2 x 10 s plus the wait for the client.
 */
esp_err_t http_client_patch(const char *url, const char *json_body);

/**
 * @brief POSTs a JSON body to @p url and copies the response body into @p response.
 *
 * @param[in]  url       Absolute https URL.
 * @param[in]  json_body NUL-terminated JSON sent as application/json.
 * @param[out] response  Receives the response body, always NUL-terminated (truncated to fit);
 *                       on non-2xx statuses it holds the error body.
 * @param[in]  size      Size of @p response in bytes.
 * @return
 *  - ESP_OK on HTTP 2xx
 *  - ESP_FAIL on any other HTTP status
 *  - ESP_ERR_INVALID_SIZE if @p url is too long
 *  - ESP_ERR_TIMEOUT if the client stayed busy for HTTP_CLIENT_LOCK_TIMEOUT_MS
 *  - ESP_ERR_HTTP_* transport errors
 * @pre Initialized; pointers not NULL and @p size > 0 (asserted).
 * @note Thread-safe. Blocking: up to 10 s plus the wait for the client.
 */
esp_err_t http_client_post(const char *url, const char *json_body, char *response, size_t size);

/**
 * @brief Opens a Server-Sent Events stream on @p url and blocks while it is open.
 *
 * Follows up to 3 redirects. Every event is delivered to @p on_event from the calling task.
 *
 * @param[in] url      Absolute https URL.
 * @param[in] on_event Receives each complete event.
 * @param[in] ctx      Forwarded to @p on_event; may be NULL.
 * @return
 *  - ESP_OK when the server closed the stream or http_client_stream_cancel() was called
 *  - ESP_ERR_TIMEOUT when no byte arrived for 60 s
 *  - ESP_ERR_INVALID_STATE if another stream is already open
 *  - ESP_ERR_INVALID_SIZE if @p url is too long
 *  - ESP_ERR_NO_MEM if no SSE parser is available
 *  - ESP_FAIL on a non-2xx status or a read error
 *  - ESP_ERR_HTTP_* transport errors
 * @pre Initialized; @p url and @p on_event are not NULL (asserted).
 * @note Thread-safe; only one stream at a time. Blocking until the stream ends.
 */
esp_err_t http_client_stream(const char *url, sse_parser_event_handler_t on_event, void *ctx);

/**
 * @brief Makes the open http_client_stream() call return ESP_OK within about 1 s.
 *
 * A request made while no stream is open applies to the next http_client_stream() call.
 *
 * @note Thread-safe, non-blocking. Not ISR-safe.
 */
void http_client_stream_cancel(void);

#endif /* HTTP_CLIENT_H */
