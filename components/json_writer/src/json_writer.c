#include "json_writer.h"

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "utils.h"

/* Records `len` characters just written into `room` bytes; a value that did not fit (len >= room)
 * marks the overflow, so everything after it is skipped. */
static void advance(json_writer_t *writer, size_t len, size_t room)
{
    if (len >= room) {
        writer->overflow = true;
    } else {
        writer->len += len;
    }
}

static void append_text(json_writer_t *writer, const char *text)
{
    if (!writer->overflow) {
        const size_t room = writer->size - writer->len;
        advance(writer, strlcpy(&writer->buffer[writer->len], text, room), room);
    }
}

void json_writer_begin(json_writer_t *writer, char *buffer, size_t size)
{
    assert((writer != NULL) && (buffer != NULL) && (size > 0U));

    *writer = (json_writer_t){.buffer = buffer, .size = size};
    buffer[0] = '\0';
    append_text(writer, "{");
}

void json_writer_add(json_writer_t *writer, const char *format, ...)
{
    assert((writer != NULL) && (format != NULL));

    if (writer->has_items) {
        append_text(writer, ",");
    }
    writer->has_items = true;
    if (writer->overflow) {
        return;
    }
    const size_t room = writer->size - writer->len;
    va_list args;
    va_start(args, format);
    const int32_t written = (int32_t)vsnprintf(&writer->buffer[writer->len], room, format, args);
    /* False positive: under -std=gnu23 (forced by ESP-IDF 6.1) va_start() expands to
     * __builtin_c23_va_start, which the clang analyzer does not model; with gnu17 it is silent. */
    va_end(args); // NOLINT(clang-analyzer-valist.Uninitialized)
    /* A negative result (encoding error) counts as not fitting. */
    advance(writer, (written < 0) ? room : (size_t)written, room);
}

void json_writer_add_catalog_item(json_writer_t *writer, const json_catalog_item_t *item)
{
    assert((item != NULL) && (item->id != NULL) && (item->type != NULL));

    json_writer_add(writer, "\"%s/type\":\"%s\"", item->id, item->type);
    if (item->unit != NULL) {
        json_writer_add(writer, "\"%s/unit\":\"%s\"", item->id, item->unit);
    }
    json_writer_add(writer, "\"%s/simulated\":%s", item->id, utils_bool_text(item->simulated));
}

esp_err_t json_writer_end(json_writer_t *writer)
{
    assert(writer != NULL);

    append_text(writer, "}");
    return writer->overflow ? ESP_ERR_INVALID_SIZE : ESP_OK;
}
