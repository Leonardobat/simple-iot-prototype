/**
 * @file reading_store.h
 * @brief Log of readings in flash (docs/adr/0004-persistencia-offline.md): the sampling task
 * appends every reading and the upload task takes them oldest first.
 *
 * Records are fixed-size and carry a CRC32; they live in segments of READING_STORE_SEGMENT_RECORDS
 * records, one file each. A closed segment is deleted once every record in it was confirmed. When
 * the file system runs low the oldest segment is dropped, sent or not.
 */
#ifndef READING_STORE_H
#define READING_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "actuator_controller.h"
#include "esp_err.h"
#include "sensor_provider.h"

/** Records in one segment (the unit of deletion and of eviction). */
#define READING_STORE_SEGMENT_RECORDS 256U

/** One reading as the sampling task captured it. */
typedef struct {
    int64_t timestamp_ms;          /**< Epoch time in milliseconds when it was sampled. */
    sensor_readings_t sensors;     /**< NaN for a sensor that is disabled or has no valid value. */
    actuator_snapshot_t actuators; /**< Real state of every actuator. */
} reading_record_t;

/**
 * @brief Opens the store on the mounted file system: validates the segments left by the previous
 * run (dropping the ones with a different format, and every older one) and starts a new segment on
 * the next append, so a record cut by a power loss is never followed by another.
 *
 * @return
 *  - ESP_OK
 *  - ESP_ERR_NO_MEM if the lock cannot be created
 *  - errors of flash_fs_list()
 * @pre flash_fs is mounted; not initialized (asserted).
 * @note Not thread-safe. Blocking.
 */
esp_err_t reading_store_init(void);

/**
 * @brief Releases the store. The segments stay in flash. Safe after a failed init.
 *
 * @note Not thread-safe: no other call may run at the same time.
 */
void reading_store_deinit(void);

/**
 * @brief Appends @p record and syncs it to flash. May drop the oldest segment first to keep free
 *        space.
 *
 * @param[in] record Reading to store.
 * @return
 *  - ESP_OK
 *  - ESP_ERR_TIMEOUT if the lock is busy
 *  - errors of flash_fs_append() (the segment is closed: the next append opens a new one)
 * @pre Initialized; @p record not NULL (asserted).
 * @note Thread-safe. Blocking (flash write).
 */
esp_err_t reading_store_append(const reading_record_t *record);

/**
 * @brief Copies the newest record appended since reading_store_init(), whether sent or not. After a
 *        reset the oldest records go out first, so the uploader uses this one to show the present
 *        at once.
 *
 * @param[out] out Receives the record.
 * @return
 *  - ESP_OK
 *  - ESP_ERR_NOT_FOUND if nothing was appended since init
 *  - ESP_ERR_TIMEOUT if the lock is busy
 * @pre Initialized; @p out not NULL (asserted).
 * @note Thread-safe, non-blocking apart from the lock. Does not consume anything.
 */
esp_err_t reading_store_latest(reading_record_t *out);

/**
 * @brief Copies the oldest unconfirmed records into @p out, without consuming them. Corrupt records
 *        are skipped (and logged). Never crosses a segment boundary, so it may return fewer than
 *        @p max_records while more are pending.
 *
 * @param[out] out         Receives up to @p max_records records, oldest first.
 * @param[in]  max_records Capacity of @p out.
 * @param[out] out_count   Records copied (0 when nothing is pending).
 * @return
 *  - ESP_OK
 *  - ESP_ERR_TIMEOUT if the lock is busy
 *  - errors of flash_fs_read()
 * @pre Initialized; pointers not NULL, @p max_records > 0 (asserted).
 * @note Thread-safe. Blocking (flash read).
 */
esp_err_t reading_store_peek(reading_record_t *out, size_t max_records, size_t *out_count);

/**
 * @brief Marks the first @p count records of the last reading_store_peek() as sent.
 *
 * @param[in] count Records to consume, at most what that peek returned.
 * @return
 *  - ESP_OK
 *  - ESP_ERR_TIMEOUT if the lock is busy
 * @pre Initialized (asserted).
 * @note Thread-safe, non-blocking apart from the lock. Only one task may peek and confirm.
 */
esp_err_t reading_store_confirm(size_t count);

#endif /* READING_STORE_H */
