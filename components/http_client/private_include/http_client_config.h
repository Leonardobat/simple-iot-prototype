/**
 * @file http_client_config.h
 * @brief Private configuration of the http_client component.
 */
#ifndef HTTP_CLIENT_CONFIG_H
#define HTTP_CLIENT_CONFIG_H

#include "http_client.h"

/** The whole request line (method, URL with query, protocol) must fit in the tx buffer. */
#define HTTP_CLIENT_TX_BUFFER_SIZE (HTTP_CLIENT_URL_MAX_LEN + 64U)

/** Connect, TLS handshake and response timeout of every request. */
#define HTTP_CLIENT_REQUEST_TIMEOUT_MS 10000

/** Longest wait for a client in use by another task: a PUT/PATCH may retry once. */
#define HTTP_CLIENT_LOCK_TIMEOUT_MS (3 * HTTP_CLIENT_REQUEST_TIMEOUT_MS)

/** Read timeout while streaming: how often a cancel request is noticed. */
#define HTTP_CLIENT_STREAM_POLL_MS 1000

/** Firebase sends a keep-alive every ~30 s, so a stream silent for longer is dead. */
#define HTTP_CLIENT_STREAM_IDLE_TIMEOUT_MS 60000U

/** Bytes read from the stream per call. */
#define HTTP_CLIENT_STREAM_READ_BUFFER_SIZE 512U

/** Redirects followed when opening a stream (Firebase may redirect to another server). */
#define HTTP_CLIENT_MAX_REDIRECTS 3U

/** esp_http_client needs a URL at creation; every request sets the real one first. */
#define HTTP_CLIENT_PLACEHOLDER_URL "https://localhost"

#define HTTP_CLIENT_CONTENT_TYPE_JSON "application/json"
#define HTTP_CLIENT_ACCEPT_SSE        "text/event-stream"

#endif /* HTTP_CLIENT_CONFIG_H */
