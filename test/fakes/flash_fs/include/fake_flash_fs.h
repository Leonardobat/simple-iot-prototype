/**
 * @file fake_flash_fs.h
 * @brief Test double of flash_fs: files in RAM, a capacity limit and injected failures.
 */
#ifndef FAKE_FLASH_FS_H
#define FAKE_FLASH_FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "flash_fs.h"

/** Most files the fake holds. */
#define FAKE_FLASH_FS_MAX_FILES 16U

/** Default capacity of the fake, in bytes. */
#define FAKE_FLASH_FS_DEFAULT_CAPACITY 65536U

typedef struct {
    char name[FLASH_FS_NAME_MAX_LEN];
    uint8_t *data;
    size_t size;
    bool used;
} fake_flash_file_t;

typedef struct {
    fake_flash_file_t files[FAKE_FLASH_FS_MAX_FILES];
    size_t capacity_bytes; /**< Total space; an append or replace past it returns ESP_ERR_NO_MEM. */
    bool mounted;
    uint32_t append_calls;
    uint32_t replace_calls;
    uint32_t remove_calls;
    esp_err_t mount_result;   /**< Returned by flash_fs_mount(). */
    esp_err_t append_result;  /**< Returned by flash_fs_append() before touching any file. */
    esp_err_t replace_result; /**< Returned by flash_fs_replace() before touching any file. */
    esp_err_t read_result;    /**< Returned by flash_fs_read() before touching any file. */
} fake_flash_fs_t;

extern fake_flash_fs_t fake_flash_fs;

/** Deletes every file and restores defaults (unmounted, default capacity, results ESP_OK). */
void fake_flash_fs_reset(void);

/** Bytes used by every file. */
size_t fake_flash_fs_used_bytes(void);

/** Flips one bit of byte @p offset of file @p name (simulates a corrupted record); false if absent.
 */
bool fake_flash_fs_corrupt(const char *name, size_t offset);

/** Cuts file @p name to @p size bytes (simulates a power cut mid-record); false if absent. */
bool fake_flash_fs_truncate(const char *name, size_t size);

#endif /* FAKE_FLASH_FS_H */
