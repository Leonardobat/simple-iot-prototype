/**
 * @file reading_store_config.h
 * @brief Private configuration of the reading_store component (file layout and limits).
 */
#ifndef READING_STORE_CONFIG_H
#define READING_STORE_CONFIG_H

#include <stdint.h>

/** Segment file name: "seg_" + 8 decimal digits + ".bin". */
#define READING_STORE_NAME_PREFIX  "seg_"
#define READING_STORE_NAME_SUFFIX  ".bin"
#define READING_STORE_NAME_FORMAT  READING_STORE_NAME_PREFIX "%08" PRIu32 READING_STORE_NAME_SUFFIX
#define READING_STORE_NAME_DIGITS  8U
#define READING_STORE_DECIMAL_BASE 10U

/** First bytes of every segment; the counts make a sensor or actuator added later invalidate old
 * segments. */
#define READING_STORE_MAGIC          0x52445347U /* "RDSG" */
#define READING_STORE_FORMAT_VERSION 1U

/** The oldest segment is dropped while fewer than this many segments' worth of bytes are free
 * (littlefs needs headroom to compact). */
#define READING_STORE_RESERVED_SEGMENTS 3U

/** Segment files the init can look at; a bigger partition is not expected (4 MB / ~14 KB). */
#define READING_STORE_MAX_SEGMENTS 512U

#define READING_STORE_LOCK_TIMEOUT_MS 5000

#endif /* READING_STORE_CONFIG_H */
