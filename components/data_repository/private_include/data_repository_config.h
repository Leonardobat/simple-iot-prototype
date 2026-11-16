/**
 * @file data_repository_config.h
 * @brief Private configuration of the data_repository component.
 */
#ifndef DATA_REPOSITORY_CONFIG_H
#define DATA_REPOSITORY_CONFIG_H

#include "data_repository.h"

/** Per reading: the key ("<timestamp_ms>": 13 digits plus quotes and colon), the braces around the
 * node and the "sensors"/"actuators" names, with some margin. */
#define DATA_REPOSITORY_BATCH_ITEM_OVERHEAD 64U

/** Request body of a full batch, including the terminating NUL. */
#define DATA_REPOSITORY_BATCH_BODY_MAX_LEN                                                         \
    (DATA_REPOSITORY_BATCH_MAX *                                                                   \
     (DATA_REPOSITORY_READING_MAX_LEN + DATA_REPOSITORY_BATCH_ITEM_OVERHEAD))

#endif /* DATA_REPOSITORY_CONFIG_H */
