/**
 * @file firebase_rtdb.h
 * @brief Firebase Realtime Database REST API (write and stream).
 *
 * Every @c path is relative to CONFIG_FIREBASE_DB_URL, without the ".json" suffix
 * (e.g. "devices/device-01"). When auth is enabled, the firebase_auth ID token is appended as
 * ?auth=<token>. URLs are built in static buffers: no heap is used per request.
 */
#ifndef FIREBASE_RTDB_H
#define FIREBASE_RTDB_H

#include "esp_err.h"
#include "sse_parser.h"

/**
 * @brief Initializes the HTTP transport, the auth cache and the URL buffers.
 *
 * @return
 *  - ESP_OK
 *  - ESP_ERR_NO_MEM if a client or mutex cannot be allocated (nothing is left allocated).
 * @pre Not initialized (asserted); the network is up before the first request.
 * @note Not thread-safe: call once at start-up.
 */
esp_err_t firebase_rtdb_init(void);

/**
 * @brief Releases everything acquired by firebase_rtdb_init(), including http_client and
 *        firebase_auth.
 *
 * @pre No request or stream is in progress (cancel the stream with firebase_rtdb_stream_cancel()
 *      and wait for firebase_rtdb_stream() to return).
 * @post The component can be initialized again.
 * @note Not thread-safe. Safe to call on a partially initialized component.
 */
void firebase_rtdb_deinit(void);

/**
 * @brief Writes @p json_body at @p path (Firebase "set": replaces the node).
 *
 * @param[in] path      Database path, e.g. "readings/device-01/1730660000000".
 * @param[in] json_body Any JSON value.
 * @return
 *  - ESP_OK
 *  - ESP_ERR_INVALID_SIZE if the URL for @p path does not fit
 *  - ESP_ERR_TIMEOUT if another write held the URL buffer for too long
 *  - errors of firebase_auth_get_token() and http_client_put()
 * @pre Initialized; @p path and @p json_body are not NULL (asserted).
 * @note Thread-safe. Blocking (network).
 */
esp_err_t firebase_rtdb_put(const char *path, const char *json_body);

/**
 * @brief Updates only the children listed in @p json_body (Firebase "update"); the rest of the node
 *        is kept.
 *
 * Keys may be paths relative to @p path (multi-path update), e.g.
 * {"temperature_1/type":"temperature"} changes one field without touching its siblings.
 *
 * @param[in] path      Database path of the node to update.
 * @param[in] json_body JSON object with the children to replace.
 * @return The same errors as firebase_rtdb_put(), with http_client_patch() as the transport.
 * @pre Initialized; @p path and @p json_body are not NULL (asserted).
 * @note Thread-safe. Blocking (network).
 */
esp_err_t firebase_rtdb_patch(const char *path, const char *json_body);

/**
 * @brief Streams the changes under @p path and blocks while the stream is open.
 *
 * Events are "put", "patch", "keep-alive", "cancel" and "auth_revoked"; Firebase closes the stream
 * with "auth_revoked" when the token expires, so the caller reconnects with a fresh one.
 *
 * @param[in] path     Database path to watch.
 * @param[in] on_event Receives every event, from the calling task.
 * @param[in] ctx      Forwarded to @p on_event; may be NULL.
 * @return
 *  - ESP_OK when the server closed the stream or firebase_rtdb_stream_cancel() was called
 *  - ESP_ERR_INVALID_STATE if a stream is already open
 *  - ESP_ERR_INVALID_SIZE if the URL for @p path does not fit
 *  - errors of firebase_auth_get_token() and http_client_stream()
 * @pre Initialized; @p path and @p on_event are not NULL (asserted).
 * @note Thread-safe; only one stream at a time. Blocking until the stream ends.
 */
esp_err_t firebase_rtdb_stream(const char *path, sse_parser_event_handler_t on_event, void *ctx);

/**
 * @brief Makes the open firebase_rtdb_stream() call return ESP_OK within about 1 s.
 *
 * @note Thread-safe, non-blocking. A request made while no stream is open applies to the next one.
 */
void firebase_rtdb_stream_cancel(void);

#endif /* FIREBASE_RTDB_H */
