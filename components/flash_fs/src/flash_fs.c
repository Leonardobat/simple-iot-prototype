#include "flash_fs.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_check.h"
#include "esp_littlefs.h"
#include "flash_fs_config.h"
#include "utils.h"

static const char *TAG = "flash_fs";

static bool s_mounted;

/* Writes "<base>/<name><suffix>" into path; ESP_ERR_INVALID_SIZE when it does not fit. */
static esp_err_t build_path(char *path, const char *name, const char *suffix)
{
    ESP_RETURN_ON_FALSE(strlen(name) < FLASH_FS_NAME_MAX_LEN, ESP_ERR_INVALID_SIZE, TAG,
                        "File name too long");
    return utils_snprintf(path, FLASH_FS_PATH_MAX_LEN,
                          FLASH_FS_BASE_PATH FLASH_FS_PATH_SEPARATOR "%s%s", name, suffix);
}

/* Maps the errno of a failed stdio call. */
static esp_err_t error_from_errno(void)
{
    if (errno == ENOSPC) {
        return ESP_ERR_NO_MEM;
    }
    return (errno == ENOENT) ? ESP_ERR_NOT_FOUND : ESP_FAIL;
}

esp_err_t flash_fs_mount(void)
{
    assert(!s_mounted);

    const esp_vfs_littlefs_conf_t config = {
        .base_path = FLASH_FS_BASE_PATH,
        .partition_label = FLASH_FS_PARTITION_LABEL,
        .format_if_mount_failed = true,
    };
    ESP_RETURN_ON_ERROR(esp_vfs_littlefs_register(&config), TAG, "Mount failed");
    s_mounted = true;
    return ESP_OK;
}

void flash_fs_unmount(void)
{
    if (!s_mounted) {
        return;
    }
    if (esp_vfs_littlefs_unregister(FLASH_FS_PARTITION_LABEL) != ESP_OK) {
        ESP_LOGW(TAG, "Unmount failed");
    }
    s_mounted = false;
}

/* Writes data to a file opened with mode and syncs it; closes the file in every case. */
static esp_err_t write_and_sync(const char *path, const char *mode, const void *data, size_t size)
{
    FILE *file = fopen(path, mode);
    if (file == NULL) {
        return error_from_errno();
    }
    const bool written =
        (fwrite(data, 1U, size, file) == size) && (fflush(file) == 0) && (fsync(fileno(file)) == 0);
    const esp_err_t err = written ? ESP_OK : error_from_errno(); /* Before fclose() sets errno. */
    const bool closed = (fclose(file) == 0);
    return ((err == ESP_OK) && !closed) ? ESP_FAIL : err;
}

esp_err_t flash_fs_append(const char *name, const void *data, size_t size)
{
    assert(s_mounted && (name != NULL) && (data != NULL));

    char path[FLASH_FS_PATH_MAX_LEN] = {0};
    ESP_RETURN_ON_ERROR(build_path(path, name, ""), TAG, "Path of %s", name);
    return write_and_sync(path, "ab", data, size);
}

/* Reads from an open file at offset; buffer->length is 0 on error. */
static esp_err_t read_at(FILE *file, size_t offset, flash_fs_buffer_t *buffer)
{
    buffer->length = 0U;
    if (fseek(file, (long)offset, SEEK_SET) != 0) {
        return ESP_FAIL;
    }
    buffer->length = fread(buffer->data, 1U, buffer->capacity, file);
    return (ferror(file) == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t flash_fs_read(const char *name, size_t offset, flash_fs_buffer_t *buffer)
{
    assert(s_mounted && (name != NULL) && (buffer != NULL) && (buffer->data != NULL));

    char path[FLASH_FS_PATH_MAX_LEN] = {0};
    ESP_RETURN_ON_ERROR(build_path(path, name, ""), TAG, "Path of %s", name);
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        buffer->length = 0U;
        return error_from_errno();
    }
    const esp_err_t err = read_at(file, offset, buffer);
    (void)fclose(file); /* Read-only: a close error loses nothing. */
    return err;
}

esp_err_t flash_fs_size(const char *name, size_t *out_size)
{
    assert(s_mounted && (name != NULL) && (out_size != NULL));

    char path[FLASH_FS_PATH_MAX_LEN] = {0};
    ESP_RETURN_ON_ERROR(build_path(path, name, ""), TAG, "Path of %s", name);
    struct stat info = {0};
    if (stat(path, &info) != 0) {
        return error_from_errno();
    }
    *out_size = (size_t)info.st_size;
    return ESP_OK;
}

esp_err_t flash_fs_replace(const char *name, const void *data, size_t size)
{
    assert(s_mounted && (name != NULL) && (data != NULL));

    char path[FLASH_FS_PATH_MAX_LEN] = {0};
    char temp_path[FLASH_FS_PATH_MAX_LEN] = {0};
    ESP_RETURN_ON_ERROR(build_path(path, name, ""), TAG, "Path of %s", name);
    ESP_RETURN_ON_ERROR(build_path(temp_path, name, FLASH_FS_TEMP_SUFFIX), TAG, "Temp path of %s",
                        name);
    ESP_RETURN_ON_ERROR(write_and_sync(temp_path, "wb", data, size), TAG, "Write of %s", name);
    if (rename(temp_path, path) != 0) {
        return error_from_errno();
    }
    return ESP_OK;
}

esp_err_t flash_fs_remove(const char *name)
{
    assert(s_mounted && (name != NULL));

    char path[FLASH_FS_PATH_MAX_LEN] = {0};
    ESP_RETURN_ON_ERROR(build_path(path, name, ""), TAG, "Path of %s", name);
    return (unlink(path) == 0) ? ESP_OK : error_from_errno();
}

esp_err_t flash_fs_free_bytes(size_t *out_free_bytes)
{
    assert(s_mounted && (out_free_bytes != NULL));

    size_t total = 0U;
    size_t used = 0U;
    ESP_RETURN_ON_ERROR(esp_littlefs_info(FLASH_FS_PARTITION_LABEL, &total, &used), TAG,
                        "Info failed");
    ESP_RETURN_ON_FALSE(used <= total, ESP_FAIL, TAG, "Inconsistent usage");
    *out_free_bytes = total - used;
    return ESP_OK;
}

esp_err_t flash_fs_list(flash_fs_name_cb_t callback, void *ctx)
{
    assert(s_mounted && (callback != NULL));

    DIR *directory = opendir(FLASH_FS_BASE_PATH);
    ESP_RETURN_ON_FALSE(directory != NULL, ESP_FAIL, TAG, "Directory not opened");
    const struct dirent *entry = readdir(directory);
    while (entry != NULL) {
        callback(entry->d_name, ctx);
        entry = readdir(directory);
    }
    (void)closedir(directory); /* Read-only: nothing to recover from a close error. */
    return ESP_OK;
}
