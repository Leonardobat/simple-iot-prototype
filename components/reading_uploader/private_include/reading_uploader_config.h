/**
 * @file reading_uploader_config.h
 * @brief Private configuration of the reading_uploader component.
 */
#ifndef READING_UPLOADER_CONFIG_H
#define READING_UPLOADER_CONFIG_H

/** Upload task stack: TLS handshake and JSON. Check the logged high water mark. */
#define READING_UPLOADER_TASK_STACK_SIZE 8192U

/** Same as the main task (the sampler), below the device_listener: a TLS handshake never delays the
 * sampler for more than one time slice. */
#define READING_UPLOADER_TASK_PRIORITY 1U

/** Time between two uploads while everything works: a backlog drains at DATA_REPOSITORY_BATCH_MAX
 * readings per period. */
#define READING_UPLOADER_PERIOD_MS 1000U

/** Wait after a failed upload: 1 s, 2 s, 4 s ... up to 30 s. */
#define READING_UPLOADER_RETRY_MAX_MS 30000U

/** Longest wait in reading_uploader_deinit(): the task may be inside a network request. */
#define READING_UPLOADER_STOP_TIMEOUT_MS 30000

#endif /* READING_UPLOADER_CONFIG_H */
