/**
 * @file utils.h
 * @brief Small helpers shared by several components (text formatting that must not overflow).
 */
#ifndef UTILS_H
#define UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * @brief Text of a boolean as a JSON literal.
 *
 * @param[in] value Value to convert.
 * @return "true" or "false" (static string, never NULL).
 * @note Thread-safe, non-blocking.
 */
const char *utils_bool_text(bool value);

/**
 * @brief snprintf() that reports truncation instead of hiding it.
 *
 * @param[out] buffer Receives the NUL-terminated text (truncated on error).
 * @param[in]  size   Size of @p buffer.
 * @param[in]  format printf format.
 * @return
 *  - ESP_OK if the whole text fits in @p size (terminator included)
 *  - ESP_ERR_INVALID_SIZE if it was truncated, empty or could not be encoded.
 * @pre @p buffer and @p format are not NULL and @p size > 0 (asserted).
 * @note Thread-safe, non-blocking.
 */
esp_err_t utils_snprintf(char *buffer, size_t size, const char *format, ...)
    __attribute__((format(printf, 3, 4))); /* GNU extension: checks the arguments (-Wformat). */

/**
 * @brief CRC-32 (IEEE 802.3, the zlib one) of @p size bytes; the check value of "123456789" is
 *        0xCBF43926. Used to detect corrupt or truncated records in flash.
 *
 * @param[in] data Bytes to cover.
 * @param[in] size Number of bytes (0 gives 0).
 * @return The CRC.
 * @pre @p data is not NULL (asserted).
 * @note Thread-safe, non-blocking. Bitwise (no table): meant for records of tens of bytes.
 */
uint32_t utils_crc32(const void *data, size_t size);

/**
 * @brief Current wall-clock time as milliseconds since the Unix epoch. It is meaningful only after
 * the SNTP synchronization (see connectivity_provider_is_time_valid()).
 *
 * @return Milliseconds since 1970-01-01 00:00:00 UTC.
 * @note Thread-safe, non-blocking.
 */
int64_t utils_epoch_ms(void);

#endif /* UTILS_H */
