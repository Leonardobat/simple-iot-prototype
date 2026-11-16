#include "data_repository.h"

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "data_repository_config.h"
#include "esp_check.h"
#include "firebase_paths.h"
#include "firebase_rtdb.h"
#include "json_writer.h"

static const char *TAG = "data_repository";

/* Request body of data_repository_push_readings(); one caller (see the header). */
static char s_batch_body[DATA_REPOSITORY_BATCH_BODY_MAX_LEN];

esp_err_t data_repository_push_readings(const data_repository_reading_t *readings, size_t count)
{
    assert(readings != NULL);
    ESP_RETURN_ON_FALSE(count > 0U, ESP_ERR_INVALID_ARG, TAG, "Empty batch");
    ESP_RETURN_ON_FALSE(count <= DATA_REPOSITORY_BATCH_MAX, ESP_ERR_INVALID_SIZE, TAG,
                        "Batch of %zu readings is too big", count);

    json_writer_t writer;
    json_writer_begin(&writer, s_batch_body, sizeof(s_batch_body));
    for (size_t i = 0U; i < count; i++) {
        assert((readings[i].sensors_json != NULL) && (readings[i].actuators_json != NULL));
        json_writer_add(&writer, "\"%" PRId64 "\":{\"sensors\":%s,\"actuators\":%s}",
                        readings[i].timestamp_ms, readings[i].sensors_json,
                        readings[i].actuators_json);
    }
    ESP_RETURN_ON_ERROR(json_writer_end(&writer), TAG, "Batch too long");
    return firebase_rtdb_patch(FIREBASE_READINGS_PATH, s_batch_body);
}

esp_err_t data_repository_publish_catalog(const char *sensors_json, const char *actuators_json)
{
    assert((sensors_json != NULL) && (actuators_json != NULL));

    ESP_RETURN_ON_ERROR(firebase_rtdb_patch(FIREBASE_SENSORS_PATH, sensors_json), TAG,
                        "Sensor catalog not published");
    return firebase_rtdb_patch(FIREBASE_ACTUATORS_PATH, actuators_json);
}
