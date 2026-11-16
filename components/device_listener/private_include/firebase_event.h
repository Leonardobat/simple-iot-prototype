/**
 * @file firebase_event.h
 * @brief Reads boolean and string fields out of Realtime Database stream events ("put" and
 * "patch").
 *
 * Firebase semantics: a "put" replaces the node at "path" with "data"; a "patch" replaces, for each
 * key of "data", the node at "path"/<key>, and keys may themselves be paths ("sensors/temp_1").
 */
#ifndef FIREBASE_EVENT_H
#define FIREBASE_EVENT_H

#include <stdbool.h>
#include <stddef.h>

#include "json_parser.h"

/** One parsed data event. */
typedef struct {
    jparse_ctx_t *json; /**< Parsed {"path":...,"data":...}, positioned at the root object. */
    const char
        *path;     /**< Value of "path", relative to the streamed node, e.g. "/" or "/sensors". */
    bool is_patch; /**< true for "patch", false for "put". */
} firebase_event_t;

/**
 * @brief Tells whether @p event changes the boolean at @p field_path and, if so, its new value.
 *
 * A field that the event deletes, or sets to anything but a boolean, reads as false.
 *
 * @param[in]  event      Event to inspect; its JSON position is left unchanged.
 * @param[in]  field_path Absolute path under the streamed node, e.g.
 * "/sensors/temperature_1/enabled"; shorter than DEVICE_LISTENER_PATH_MAX_LEN.
 * @param[out] out_value  New value; written only when true is returned.
 * @return true if the event changes the field, false if it leaves it as it was.
 * @pre Pointers not NULL, @p field_path starts with '/' and fits (asserted).
 * @note Not thread-safe for the same event; non-blocking.
 */
bool firebase_event_bool_at(const firebase_event_t *event, const char *field_path, bool *out_value);

/**
 * @brief Tells whether @p event changes the string at @p field_path and, if so, its new value.
 *
 * A field that the event deletes, sets to anything but a string, or sets to a string that does not
 * fit in @p size bytes reads as "".
 *
 * @param[in]  event      Event to inspect; its JSON position is left unchanged.
 * @param[in]  field_path Absolute path under the streamed node, e.g.
 * "/actuators/rgb_light_1/color"; shorter than DEVICE_LISTENER_PATH_MAX_LEN.
 * @param[out] out        New value, NUL-terminated; written only when true is returned.
 * @param[in]  size       Size of @p out (> 0).
 * @return true if the event changes the field, false if it leaves it as it was.
 * @pre Pointers not NULL, @p size > 0, @p field_path starts with '/' and fits (asserted).
 * @note Not thread-safe for the same event; non-blocking.
 */
bool firebase_event_string_at(const firebase_event_t *event, const char *field_path, char *out,
                              size_t size);

#endif /* FIREBASE_EVENT_H */
