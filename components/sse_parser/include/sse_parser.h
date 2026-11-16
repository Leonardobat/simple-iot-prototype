/**
 * @file sse_parser.h
 * @brief Incremental Server-Sent Events (SSE) parser.
 *
 * Bytes of a text/event-stream response are fed as they arrive; each complete event (terminated
 * by a blank line) is delivered to a handler. Parsers come from a static pool: no heap is used.
 */
#ifndef SSE_PARSER_H
#define SSE_PARSER_H

#include <stddef.h>

#include "esp_err.h"

/** Longest accepted event name, including the terminating NUL. */
#define SSE_PARSER_EVENT_NAME_MAX_LEN 32U

/** Longest accepted line (field name and value), including the terminating NUL. */
#define SSE_PARSER_LINE_MAX_LEN 2048U

/**
 * @brief Receives one complete event.
 *
 * @param[in] event Value of the event's "event:" field (never empty).
 * @param[in] data  Value of the event's last "data:" line, or "" when it has none.
 * @param[in] ctx   Pointer given to sse_parser_init().
 * @note Both strings are only valid during the call.
 */
typedef void (*sse_parser_event_handler_t)(const char *event, const char *data, void *ctx);

/** Opaque parser handle. */
typedef struct sse_parser_s *sse_parser_handle_t;

/**
 * @brief Takes a parser from the static pool.
 *
 * @param[in]  on_event   Called for every complete event that has an "event:" field.
 * @param[in]  ctx        Forwarded to @p on_event; may be NULL.
 * @param[out] out_parser Receives the handle.
 * @return
 *  - ESP_OK
 *  - ESP_ERR_NO_MEM when every parser of the pool is in use.
 * @pre @p on_event and @p out_parser are not NULL (asserted).
 * @post On success the parser is empty and owned by the caller until sse_parser_deinit().
 * @note Thread-safe, non-blocking. Not ISR-safe.
 */
esp_err_t sse_parser_init(sse_parser_event_handler_t on_event, void *ctx,
                          sse_parser_handle_t *out_parser);

/**
 * @brief Returns the parser to the pool.
 *
 * @param[in] parser Handle from sse_parser_init().
 * @pre @p parser is in use (asserted).
 * @post @p parser must not be used again.
 * @note Thread-safe, non-blocking. Not ISR-safe.
 */
void sse_parser_deinit(sse_parser_handle_t parser);

/**
 * @brief Discards any partial line and event, e.g. before parsing a new connection.
 *
 * @param[in] parser Handle from sse_parser_init().
 * @pre @p parser is in use (asserted).
 * @note Not thread-safe: one task owns a parser at a time.
 */
void sse_parser_reset(sse_parser_handle_t parser);

/**
 * @brief Feeds raw stream bytes; chunks may split lines and events at any point.
 *
 * "\r\n" and "\n" line endings are accepted; comments and unknown fields are ignored. A line longer
 * than SSE_PARSER_LINE_MAX_LEN - 1 bytes drops the whole event it belongs to.
 *
 * @param[in] parser Handle from sse_parser_init().
 * @param[in] chunk  Bytes to parse (not NUL-terminated); may be NULL only when @p len is 0.
 * @param[in] len    Number of bytes in @p chunk.
 * @pre @p parser is in use (asserted).
 * @note Not thread-safe: one task owns a parser at a time. Invokes the handler synchronously.
 */
void sse_parser_feed(sse_parser_handle_t parser, const char *chunk, size_t len);

#endif /* SSE_PARSER_H */
