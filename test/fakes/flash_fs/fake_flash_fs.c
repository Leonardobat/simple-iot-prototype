#include "fake_flash_fs.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

fake_flash_fs_t fake_flash_fs;

static fake_flash_file_t *find(const char *name)
{
    for (uint32_t i = 0U; i < FAKE_FLASH_FS_MAX_FILES; i++) {
        if (fake_flash_fs.files[i].used && (strcmp(fake_flash_fs.files[i].name, name) == 0)) {
            return &fake_flash_fs.files[i];
        }
    }
    return NULL;
}

static fake_flash_file_t *create(const char *name)
{
    for (uint32_t i = 0U; i < FAKE_FLASH_FS_MAX_FILES; i++) {
        fake_flash_file_t *file = &fake_flash_fs.files[i];
        if (!file->used) {
            memset(file, 0, sizeof(*file));
            (void)strlcpy(file->name, name, sizeof(file->name));
            file->used = true;
            return file;
        }
    }
    return NULL;
}

static void release(fake_flash_file_t *file)
{
    free(file->data);
    memset(file, 0, sizeof(*file));
}

size_t fake_flash_fs_used_bytes(void)
{
    size_t used = 0U;
    for (uint32_t i = 0U; i < FAKE_FLASH_FS_MAX_FILES; i++) {
        used += fake_flash_fs.files[i].size;
    }
    return used;
}

void fake_flash_fs_reset(void)
{
    for (uint32_t i = 0U; i < FAKE_FLASH_FS_MAX_FILES; i++) {
        release(&fake_flash_fs.files[i]);
    }
    memset(&fake_flash_fs, 0, sizeof(fake_flash_fs));
    fake_flash_fs.capacity_bytes = FAKE_FLASH_FS_DEFAULT_CAPACITY;
}

bool fake_flash_fs_corrupt(const char *name, size_t offset)
{
    fake_flash_file_t *file = find(name);
    if ((file == NULL) || (offset >= file->size)) {
        return false;
    }
    file->data[offset] ^= 0x01U;
    return true;
}

bool fake_flash_fs_truncate(const char *name, size_t size)
{
    fake_flash_file_t *file = find(name);
    if ((file == NULL) || (size > file->size)) {
        return false;
    }
    file->size = size;
    return true;
}

esp_err_t flash_fs_mount(void)
{
    assert(!fake_flash_fs.mounted);
    if (fake_flash_fs.mount_result == ESP_OK) {
        fake_flash_fs.mounted = true;
    }
    return fake_flash_fs.mount_result;
}

void flash_fs_unmount(void)
{
    fake_flash_fs.mounted = false;
}

/* Replaces the content of file with size bytes of data appended after keep bytes of the old one. */
static esp_err_t store(fake_flash_file_t *file, size_t keep, const void *data, size_t size)
{
    const size_t used_without_file = fake_flash_fs_used_bytes() - file->size;
    if ((used_without_file + keep + size) > fake_flash_fs.capacity_bytes) {
        return ESP_ERR_NO_MEM;
    }
    uint8_t *grown = realloc(file->data, keep + size + 1U);
    if (grown == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(&grown[keep], data, size);
    file->data = grown;
    file->size = keep + size;
    return ESP_OK;
}

esp_err_t flash_fs_append(const char *name, const void *data, size_t size)
{
    assert(fake_flash_fs.mounted && (name != NULL) && (data != NULL));
    fake_flash_fs.append_calls++;
    if (fake_flash_fs.append_result != ESP_OK) {
        return fake_flash_fs.append_result;
    }
    if (strlen(name) >= FLASH_FS_NAME_MAX_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }
    fake_flash_file_t *file = find(name);
    if (file == NULL) {
        file = create(name);
    }
    if (file == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return store(file, file->size, data, size);
}

esp_err_t flash_fs_read(const char *name, size_t offset, flash_fs_buffer_t *buffer)
{
    assert(fake_flash_fs.mounted && (name != NULL) && (buffer != NULL) && (buffer->data != NULL));
    buffer->length = 0U;
    if (fake_flash_fs.read_result != ESP_OK) {
        return fake_flash_fs.read_result;
    }
    const fake_flash_file_t *file = find(name);
    if (file == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    if (offset < file->size) {
        const size_t available = file->size - offset;
        buffer->length = (available < buffer->capacity) ? available : buffer->capacity;
        memcpy(buffer->data, &file->data[offset], buffer->length);
    }
    return ESP_OK;
}

esp_err_t flash_fs_size(const char *name, size_t *out_size)
{
    assert(fake_flash_fs.mounted && (name != NULL) && (out_size != NULL));
    const fake_flash_file_t *file = find(name);
    if (file == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    *out_size = file->size;
    return ESP_OK;
}

esp_err_t flash_fs_replace(const char *name, const void *data, size_t size)
{
    assert(fake_flash_fs.mounted && (name != NULL) && (data != NULL));
    fake_flash_fs.replace_calls++;
    if (fake_flash_fs.replace_result != ESP_OK) {
        return fake_flash_fs.replace_result;
    }
    if (strlen(name) >= FLASH_FS_NAME_MAX_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }
    fake_flash_file_t *file = find(name);
    if (file == NULL) {
        file = create(name);
    }
    if (file == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return store(file, 0U, data, size);
}

esp_err_t flash_fs_remove(const char *name)
{
    assert(fake_flash_fs.mounted && (name != NULL));
    fake_flash_fs.remove_calls++;
    fake_flash_file_t *file = find(name);
    if (file == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    release(file);
    return ESP_OK;
}

esp_err_t flash_fs_free_bytes(size_t *out_free_bytes)
{
    assert(fake_flash_fs.mounted && (out_free_bytes != NULL));
    const size_t used = fake_flash_fs_used_bytes();
    *out_free_bytes =
        (used < fake_flash_fs.capacity_bytes) ? (fake_flash_fs.capacity_bytes - used) : 0U;
    return ESP_OK;
}

esp_err_t flash_fs_list(flash_fs_name_cb_t callback, void *ctx)
{
    assert(fake_flash_fs.mounted && (callback != NULL));
    for (uint32_t i = 0U; i < FAKE_FLASH_FS_MAX_FILES; i++) {
        if (fake_flash_fs.files[i].used) {
            callback(fake_flash_fs.files[i].name, ctx);
        }
    }
    return ESP_OK;
}
