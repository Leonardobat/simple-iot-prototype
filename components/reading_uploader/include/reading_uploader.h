/**
 * @file reading_uploader.h
 * @brief Task that sends the readings kept in flash to the Realtime Database, oldest first
 *        (docs/adr/0004-persistencia-offline.md), after publishing the catalog once.
 */
#ifndef READING_UPLOADER_H
#define READING_UPLOADER_H

#include <stdbool.h>

#include "esp_err.h"

/** Tells whether the network can be tried; the uploader waits while it returns false. */
typedef bool (*reading_uploader_online_fn_t)(void);

/**
 * @brief Starts the upload task: every second it publishes the catalog (until that works) and sends
 *        up to DATA_REPOSITORY_BATCH_MAX stored readings; after a failure it waits with exponential
 *        backoff (1 s up to 30 s).
 *
 * @param[in] is_online Called by the task before each attempt.
 * @return
 *  - ESP_OK
 *  - ESP_ERR_NO_MEM if the task cannot be created
 * @pre reading_store and firebase_rtdb are initialized; not initialized (asserted); @p is_online
 * not NULL (asserted).
 * @note Not thread-safe. The stored readings are consumed only by this task.
 */
esp_err_t reading_uploader_init(reading_uploader_online_fn_t is_online);

/**
 * @brief Stops the task, waiting for the request in progress, and forgets that the catalog was
 *        published. Safe when not initialized.
 *
 * @return
 *  - ESP_OK
 *  - ESP_ERR_TIMEOUT if the task did not stop in time
 * @note Not thread-safe. Blocking: up to 30 s.
 */
esp_err_t reading_uploader_deinit(void);

/**
 * @brief One upload attempt: publishes the catalog if it was not yet, then sends one batch of the
 *        oldest stored readings and consumes them on success. Nothing stored is not an error.
 *
 *  The task calls this; it is public so tests can run an attempt without waiting for the task.
 *
 * @return
 *  - ESP_OK
 *  - errors of data_repository_publish_catalog(), reading_store_peek() or
 *    data_repository_push_readings(); the stored readings stay for the next attempt
 * @pre reading_store and firebase_rtdb are initialized.
 * @note Not thread-safe: only the upload task (or a test while the task is offline) calls it.
 *       Blocking (network).
 */
esp_err_t reading_uploader_step(void);

#endif /* READING_UPLOADER_H */
