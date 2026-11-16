#include "config_store.h"

#include <assert.h>
#include <stddef.h>

#include "config_store_config.h"
#include "esp_check.h"
#include "flash_fs.h"
#include "utils.h"

static const char *TAG = "config_store";

/* File: version, then the two masks, then the CRC32 of the bytes before it. */
typedef struct {
    uint32_t version;
    uint32_t sensors;
    uint32_t actuators;
    uint32_t crc;
} stored_config_t;

static_assert(sizeof(stored_config_t) == (4U * sizeof(uint32_t)), "stored_config_t has padding");

static uint32_t crc_of(const stored_config_t *stored)
{
    return utils_crc32(stored, offsetof(stored_config_t, crc));
}

esp_err_t config_store_load(config_store_enabled_t *out)
{
    assert(out != NULL);

    stored_config_t stored = {0};
    flash_fs_buffer_t buffer = {.data = &stored, .capacity = sizeof(stored), .length = 0U};
    out->sensors = 0U;
    out->actuators = 0U;
    ESP_RETURN_ON_ERROR(flash_fs_read(CONFIG_STORE_FILE_NAME, 0U, &buffer), TAG, "Read failed");
    ESP_RETURN_ON_FALSE((buffer.length == sizeof(stored)) &&
                            (stored.version == CONFIG_STORE_FORMAT_VERSION) &&
                            (stored.crc == crc_of(&stored)),
                        ESP_ERR_INVALID_RESPONSE, TAG, "Saved config is corrupt");
    out->sensors = stored.sensors;
    out->actuators = stored.actuators;
    return ESP_OK;
}

esp_err_t config_store_save(const config_store_enabled_t *enabled)
{
    assert(enabled != NULL);

    stored_config_t stored = {
        .version = CONFIG_STORE_FORMAT_VERSION,
        .sensors = enabled->sensors,
        .actuators = enabled->actuators,
        .crc = 0U,
    };
    stored.crc = crc_of(&stored);
    return flash_fs_replace(CONFIG_STORE_FILE_NAME, &stored, sizeof(stored));
}
