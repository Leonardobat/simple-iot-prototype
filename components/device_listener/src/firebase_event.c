#include "firebase_event.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "device_listener_config.h"

/* Path of `field_path` relative to `event_path`, with no leading '/': "" when both are equal, NULL
 * when the field is not under the event path. */
static const char *relative_path(const char *event_path, const char *field_path)
{
    if (strcmp(event_path, "/") == 0) {
        return &field_path[1];
    }
    const size_t len = strlen(event_path);
    if (strncmp(event_path, field_path, len) != 0) {
        return NULL;
    }
    if (field_path[len] == '\0') {
        return &field_path[len];
    }
    return (field_path[len] == '/') ? &field_path[len + 1U] : NULL;
}

/* Cuts `segments` at its first '/' and returns what follows, or NULL if it was the last segment. */
static char *split_segment(char *segments)
{
    char *slash = strchr(segments, '/');
    if (slash == NULL) {
        return NULL;
    }
    *slash = '\0';
    return &slash[1];
}

/* Reads the field `name` of the object at json->cur into `out`; `name` is NULL when the field (or
 * one of its parents) is missing. Writes a default when the field is missing or of another type. */
typedef void (*value_reader_t)(jparse_ctx_t *json, const char *name, void *out);

/* What to read and where to store it. */
typedef struct {
    value_reader_t read;
    void *out;
} value_request_t;

typedef struct {
    char *buffer;
    size_t size;
} string_out_t;

static void read_bool_value(jparse_ctx_t *json, const char *name, void *out)
{
    bool *value = out;
    if ((name == NULL) || (json_obj_get_bool(json, name, value) != OS_SUCCESS)) {
        *value = false;
    }
}

static void read_string_value(jparse_ctx_t *json, const char *name, void *out)
{
    const string_out_t *text = out;
    if ((name == NULL) ||
        (json_obj_get_string(json, name, text->buffer, (int)text->size) != OS_SUCCESS)) {
        text->buffer[0] = '\0';
    }
}

/* Reads the value at <key>/<rest> under the current object. Leaves the JSON position as it was. */
static void read_value(jparse_ctx_t *json, const char *key, const char *rest,
                       const value_request_t *request)
{
    char segments[DEVICE_LISTENER_PATH_MAX_LEN] = {0};
    (void)strlcpy(segments, rest, sizeof(segments)); /* Cannot truncate: suffix of a field path. */
    json_tok_t *const start = json->cur;

    const char *name = key;
    char *next = (segments[0] != '\0') ? segments : NULL;
    bool found = true;
    while (found && (next != NULL)) {
        found = json_obj_get_object(json, name) == OS_SUCCESS;
        name = next;
        next = split_segment(next);
    }
    request->read(json, found ? name : NULL, request->out);
    json->cur = start;
}

/* For a patch: whether "data" has the key rest[0, len) and, if so, the value it gives the field. */
static bool patch_key_sets(jparse_ctx_t *json, const char *rest, size_t len,
                           const value_request_t *request)
{
    char key[DEVICE_LISTENER_PATH_MAX_LEN] = {0};
    assert(len < sizeof(key));
    memcpy(key, rest, len);
    if (json_obj_get_val_tok(json, key, JSMN_UNDEFINED) == NULL) {
        return false;
    }
    read_value(json, key, (rest[len] == '/') ? &rest[len + 1U] : "", request);
    return true;
}

/* Tries every key that could hold the field: "sensors", "sensors/temp_1", "sensors/temp_1/enabled".
 */
static bool patch_sets(jparse_ctx_t *json, const char *rest, const value_request_t *request)
{
    json_tok_t *const start = json->cur;
    if ((rest[0] == '\0') || (json_obj_get_object(json, "data") != OS_SUCCESS)) {
        return false; /* A patch of the field itself would give children to a scalar. */
    }
    const size_t rest_len = strlen(rest);
    bool changed = false;
    for (size_t len = 1U; (len <= rest_len) && !changed; len++) {
        if ((rest[len] == '/') || (rest[len] == '\0')) {
            changed = patch_key_sets(json, rest, len, request);
        }
    }
    json->cur = start;
    return changed;
}

static bool event_value_at(const firebase_event_t *event, const char *field_path,
                           const value_request_t *request)
{
    assert((event != NULL) && (event->json != NULL) && (event->path != NULL));
    assert(field_path != NULL);
    assert((field_path[0] == '/') && (strlen(field_path) < DEVICE_LISTENER_PATH_MAX_LEN));

    const char *rest = relative_path(event->path, field_path);
    if (rest == NULL) {
        return false;
    }
    if (event->is_patch) {
        return patch_sets(event->json, rest, request);
    }
    read_value(event->json, "data", rest, request);
    return true;
}

bool firebase_event_bool_at(const firebase_event_t *event, const char *field_path, bool *out_value)
{
    assert(out_value != NULL);
    bool value = false;
    const value_request_t request = {read_bool_value, &value};
    const bool changed = event_value_at(event, field_path, &request);
    if (changed) {
        *out_value = value;
    }
    return changed;
}

bool firebase_event_string_at(const firebase_event_t *event, const char *field_path, char *out,
                              size_t size)
{
    assert((out != NULL) && (size > 0U));
    string_out_t text = {out, size};
    const value_request_t request = {read_string_value, &text};
    return event_value_at(event, field_path, &request);
}
