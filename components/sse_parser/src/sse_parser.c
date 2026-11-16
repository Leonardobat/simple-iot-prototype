#include "sse_parser.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "sse_parser_config.h"

#define FIELD_EVENT_LEN (sizeof(SSE_PARSER_FIELD_EVENT) - 1U)
#define FIELD_DATA_LEN  (sizeof(SSE_PARSER_FIELD_DATA) - 1U)

struct sse_parser_s {
    bool in_use;
    sse_parser_event_handler_t on_event;
    void *ctx;
    char event[SSE_PARSER_EVENT_NAME_MAX_LEN];
    char data[SSE_PARSER_LINE_MAX_LEN];
    char line[SSE_PARSER_LINE_MAX_LEN];
    size_t line_len;
    bool line_overflow;
};

static const char *TAG = "sse_parser";

static struct sse_parser_s s_pool[SSE_PARSER_POOL_SIZE];
static portMUX_TYPE s_pool_lock = portMUX_INITIALIZER_UNLOCKED;

static void reset_event(sse_parser_handle_t parser)
{
    parser->event[0] = '\0';
    parser->data[0] = '\0';
}

static void reset_line(sse_parser_handle_t parser)
{
    parser->line_len = 0U;
    parser->line_overflow = false;
}

static void copy_field_value(char *dst, size_t size, const char *value)
{
    const char *start = (*value == ' ') ? (value + 1) : value;
    /* Truncation cannot happen: dst is at least as large as the line holding the value. */
    (void)strlcpy(dst, start, size);
}

static void dispatch_event(sse_parser_handle_t parser)
{
    if (parser->event[0] != '\0') {
        parser->on_event(parser->event, parser->data, parser->ctx);
    }
    reset_event(parser);
}

static void process_line(sse_parser_handle_t parser)
{
    parser->line[parser->line_len] = '\0';

    if (parser->line_overflow) {
        ESP_LOGW(TAG, "SSE line exceeds %u bytes, dropping event",
                 (unsigned)SSE_PARSER_LINE_MAX_LEN);
        reset_event(parser);
    } else if (parser->line_len == 0U) {
        dispatch_event(parser);
    } else if (strncmp(parser->line, SSE_PARSER_FIELD_EVENT, FIELD_EVENT_LEN) == 0) {
        copy_field_value(parser->event, sizeof(parser->event), &parser->line[FIELD_EVENT_LEN]);
    } else if (strncmp(parser->line, SSE_PARSER_FIELD_DATA, FIELD_DATA_LEN) == 0) {
        copy_field_value(parser->data, sizeof(parser->data), &parser->line[FIELD_DATA_LEN]);
    } else {
        /* Comments (":...") and the "id:"/"retry:" fields are not used by Firebase. */
    }
    reset_line(parser);
}

static void append_char(sse_parser_handle_t parser, char c)
{
    if (parser->line_len < (sizeof(parser->line) - 1U)) {
        parser->line[parser->line_len] = c;
        parser->line_len++;
    } else {
        parser->line_overflow = true;
    }
}

esp_err_t sse_parser_init(sse_parser_event_handler_t on_event, void *ctx,
                          sse_parser_handle_t *out_parser)
{
    assert(on_event != NULL);
    assert(out_parser != NULL);

    sse_parser_handle_t parser = NULL;
    portENTER_CRITICAL(&s_pool_lock);
    for (size_t i = 0U; (i < SSE_PARSER_POOL_SIZE) && (parser == NULL); i++) {
        if (!s_pool[i].in_use) {
            s_pool[i].in_use = true;
            parser = &s_pool[i];
        }
    }
    portEXIT_CRITICAL(&s_pool_lock);

    if (parser == NULL) {
        return ESP_ERR_NO_MEM;
    }
    parser->on_event = on_event;
    parser->ctx = ctx;
    sse_parser_reset(parser);
    *out_parser = parser;
    return ESP_OK;
}

void sse_parser_deinit(sse_parser_handle_t parser)
{
    assert((parser != NULL) && parser->in_use);

    portENTER_CRITICAL(&s_pool_lock);
    parser->in_use = false;
    portEXIT_CRITICAL(&s_pool_lock);
}

void sse_parser_reset(sse_parser_handle_t parser)
{
    assert((parser != NULL) && parser->in_use);

    reset_event(parser);
    reset_line(parser);
}

void sse_parser_feed(sse_parser_handle_t parser, const char *chunk, size_t len)
{
    assert((parser != NULL) && parser->in_use);
    assert((chunk != NULL) || (len == 0U));

    for (size_t i = 0U; i < len; i++) {
        const char c = chunk[i];
        if (c == '\n') {
            process_line(parser);
        } else if (c != '\r') {
            append_char(parser, c);
        } else {
            /* '\r' is part of the "\r\n" line ending and is dropped. */
        }
    }
}
