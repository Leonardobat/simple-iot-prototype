/**
 * @file json_writer.h
 * @brief Builds a flat JSON object into a caller buffer, member by member, without overflowing it.
 *
 * Callers format each member themselves, so keys and string values must already be valid JSON
 * (no '"' or '\\' to escape).
 *
 * @code
 * json_writer_t writer;
 * json_writer_begin(&writer, buffer, sizeof(buffer));
 * json_writer_add(&writer, "\"%s\":%.1f", "temperature_1", 22.4);
 * esp_err_t err = json_writer_end(&writer); // buffer = {"temperature_1":22.4}
 * @endcode
 */
#ifndef JSON_WRITER_H
#define JSON_WRITER_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/** State of one object being written (plain data, owned by the caller). */
typedef struct {
    char *buffer;   /**< Destination, always NUL-terminated. */
    size_t size;    /**< Size of @c buffer in bytes. */
    size_t len;     /**< Characters written so far. */
    bool has_items; /**< A member was added: the next one needs a comma. */
    bool overflow;  /**< Something did not fit; the rest is skipped. */
} json_writer_t;

/**
 * @brief Starts an object: writes "{".
 *
 * @param[out] writer State to initialize.
 * @param[in]  buffer Destination.
 * @param[in]  size   Size of @p buffer (> 0).
 * @pre @p writer and @p buffer are not NULL and @p size > 0 (asserted).
 * @note Thread-safe for distinct writers. Non-blocking.
 */
void json_writer_begin(json_writer_t *writer, char *buffer, size_t size);

/**
 * @brief Appends one member, preceded by a comma when needed.
 *
 * @param[in,out] writer State from json_writer_begin().
 * @param[in]     format printf format of the member, e.g. "\"%s\":%d".
 * @pre @p writer and @p format are not NULL (asserted).
 * @note Does nothing once the buffer has overflowed; json_writer_end() reports it.
 */
void json_writer_add(json_writer_t *writer, const char *format, ...)
    __attribute__((format(printf, 2, 3))); /* GNU extension: checks the arguments (-Wformat). */

/** Device-owned fields of one catalog item (plain data, copied by value). */
typedef struct {
    const char *id;   /**< Database key of the item. */
    const char *type; /**< Item type. */
    const char *unit; /**< Unit, already JSON-escaped, or NULL for none. */
    bool simulated;   /**< Whether the item is mocked. */
} json_catalog_item_t;

/**
 * @brief Appends the device-owned fields of one catalog item as multi-path members:
 *        "<id>/type", "<id>/unit" (only when @p unit is not NULL) and "<id>/simulated".
 *
 * A PATCH with these members keeps the fields owned by the SPA (e.g. "enabled").
 *
 * @param[in,out] writer State from json_writer_begin().
 * @param[in]     item   Item to describe.
 * @pre @p writer and @p item are not NULL, and so are item->id and item->type (asserted).
 * @note Does nothing once the buffer has overflowed; json_writer_end() reports it.
 */
void json_writer_add_catalog_item(json_writer_t *writer, const json_catalog_item_t *item);

/**
 * @brief Closes the object: writes "}".
 *
 * @param[in,out] writer State from json_writer_begin().
 * @return
 *  - ESP_OK with the complete object in the buffer
 *  - ESP_ERR_INVALID_SIZE if anything did not fit (the buffer holds a truncated prefix).
 * @pre @p writer is not NULL (asserted).
 */
esp_err_t json_writer_end(json_writer_t *writer);

#endif /* JSON_WRITER_H */
