#include "utils.h"

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>

#define UTILS_CRC32_INITIAL    0xFFFFFFFFU
#define UTILS_CRC32_POLYNOMIAL 0xEDB88320U /* Reflected 0x04C11DB7. */
#define UTILS_BITS_PER_BYTE    8U
#define UTILS_MS_PER_S         1000LL
#define UTILS_US_PER_MS        1000LL

const char *utils_bool_text(bool value)
{
    return value ? "true" : "false";
}

esp_err_t utils_snprintf(char *buffer, size_t size, const char *format, ...)
{
    assert((buffer != NULL) && (format != NULL) && (size > 0U));

    va_list args;
    va_start(args, format);
    const int32_t written = (int32_t)vsnprintf(buffer, size, format, args);
    /* False positive: see json_writer_add() (va_start is __builtin_c23_va_start under gnu23). */
    va_end(args); // NOLINT(clang-analyzer-valist.Uninitialized)
    return ((written > 0) && ((size_t)written < size)) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

uint32_t utils_crc32(const void *data, size_t size)
{
    assert(data != NULL);

    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = UTILS_CRC32_INITIAL;
    for (size_t i = 0U; i < size; i++) {
        crc ^= bytes[i];
        for (uint32_t bit = 0U; bit < UTILS_BITS_PER_BYTE; bit++) {
            crc = ((crc & 1U) != 0U) ? ((crc >> 1U) ^ UTILS_CRC32_POLYNOMIAL) : (crc >> 1U);
        }
    }
    return crc ^ UTILS_CRC32_INITIAL;
}

int64_t utils_epoch_ms(void)
{
    struct timeval now = {0};
    /* Cannot fail with a valid pointer and no timezone argument. */
    (void)gettimeofday(&now, NULL);
    return ((int64_t)now.tv_sec * UTILS_MS_PER_S) + ((int64_t)now.tv_usec / UTILS_US_PER_MS);
}
