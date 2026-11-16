/**
 * @file flash_fs.h
 * @brief HAL of the flash file system (LittleFS on the "storage" partition): flat namespace of
 * small files, thin wrapper over joltwallet/littlefs.
 *
 * Replaced by an in-RAM fake in the unit tests (test/fakes/flash_fs).
 */
#ifndef FLASH_FS_H
#define FLASH_FS_H

#include <stddef.h>

#include "esp_err.h"

/** Longest file name, including the terminating NUL. */
#define FLASH_FS_NAME_MAX_LEN 32U

/** Called by flash_fs_list() once per file; @p name is valid only during the call. */
typedef void (*flash_fs_name_cb_t)(const char *name, void *ctx);

/**
 * @brief Mounts the file system, formatting the partition when it has none.
 *
 * @return
 *  - ESP_OK
 *  - errors of the LittleFS driver (partition missing, format failed)
 * @pre Not mounted (asserted).
 * @note Not thread-safe. Blocking (a format takes seconds).
 */
esp_err_t flash_fs_mount(void);

/**
 * @brief Unmounts the file system. Safe when the mount failed.
 *
 * @note Not thread-safe.
 */
void flash_fs_unmount(void);

/**
 * @brief Appends @p size bytes to file @p name (created if missing) and syncs them to flash before
 *        returning, so a power cut loses at most this call.
 *
 * @param[in] name Valid file name (shorter than FLASH_FS_NAME_MAX_LEN).
 * @param[in] data Bytes to append.
 * @param[in] size Number of bytes.
 * @return
 *  - ESP_OK
 *  - ESP_ERR_INVALID_SIZE if @p name is too long
 *  - ESP_ERR_NO_MEM if the file system is full
 *  - ESP_FAIL on any other I/O error
 * @pre Mounted (asserted); pointers not NULL (asserted).
 * @note Thread-safe. Blocking (flash erase and write).
 */
esp_err_t flash_fs_append(const char *name, const void *data, size_t size);

/** Buffer that flash_fs_read() fills. */
typedef struct {
    void *data;      /**< Receives the bytes. */
    size_t capacity; /**< Size of @c data. */
    size_t length;   /**< Set by flash_fs_read(): bytes read (0 at or past the end of the file). */
} flash_fs_buffer_t;

/**
 * @brief Reads up to @c buffer->capacity bytes of file @p name starting at @p offset.
 *
 * @param[in]     name   File name.
 * @param[in]     offset First byte to read.
 * @param[in,out] buffer Where to read to; its @c length is set (0 on error).
 * @return
 *  - ESP_OK
 *  - ESP_ERR_NOT_FOUND if the file does not exist
 *  - ESP_FAIL on an I/O error
 * @pre Mounted (asserted); pointers not NULL (asserted).
 * @note Thread-safe. Blocking.
 */
esp_err_t flash_fs_read(const char *name, size_t offset, flash_fs_buffer_t *buffer);

/**
 * @brief Gets the size in bytes of file @p name.
 *
 * @return
 *  - ESP_OK
 *  - ESP_ERR_NOT_FOUND if the file does not exist
 * @pre Mounted (asserted); pointers not NULL (asserted).
 * @note Thread-safe. Blocking.
 */
esp_err_t flash_fs_size(const char *name, size_t *out_size);

/**
 * @brief Replaces the whole content of file @p name atomically: either the old or the new content
 *        survives a power cut (temporary file, sync and rename).
 *
 * @return
 *  - ESP_OK
 *  - ESP_ERR_INVALID_SIZE if @p name is too long
 *  - ESP_ERR_NO_MEM if the file system is full
 *  - ESP_FAIL on any other I/O error
 * @pre Mounted (asserted); pointers not NULL (asserted).
 * @note Thread-safe. Blocking.
 */
esp_err_t flash_fs_replace(const char *name, const void *data, size_t size);

/**
 * @brief Deletes file @p name.
 *
 * @return
 *  - ESP_OK
 *  - ESP_ERR_NOT_FOUND if the file does not exist
 * @pre Mounted (asserted); pointer not NULL (asserted).
 * @note Thread-safe. Blocking.
 */
esp_err_t flash_fs_remove(const char *name);

/**
 * @brief Gets the free space of the file system.
 *
 * @param[out] out_free_bytes Free bytes.
 * @return
 *  - ESP_OK
 *  - ESP_FAIL on an I/O error
 * @pre Mounted (asserted); pointer not NULL (asserted).
 * @note Thread-safe. Blocking.
 */
esp_err_t flash_fs_free_bytes(size_t *out_free_bytes);

/**
 * @brief Calls @p callback with the name of every file.
 *
 * @return
 *  - ESP_OK
 *  - ESP_FAIL on an I/O error
 * @pre Mounted (asserted); @p callback not NULL (asserted).
 * @note Thread-safe. Blocking. @p callback must not call back into flash_fs.
 */
esp_err_t flash_fs_list(flash_fs_name_cb_t callback, void *ctx);

#endif /* FLASH_FS_H */
