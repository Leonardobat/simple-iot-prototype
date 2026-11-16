/**
 * @file backoff.h
 * @brief Exponential backoff with a ceiling, shared by the reconnect loops (Wi-Fi, Firebase
 * stream).
 *
 * Stateless: the caller keeps the current delay and resets it to 0 after a successful attempt.
 */
#ifndef BACKOFF_H
#define BACKOFF_H

#include <stdint.h>

/**
 * @brief Returns the delay to wait before the next retry.
 *
 * The first retry (@p previous_ms below @p initial_ms, e.g. 0) waits @p initial_ms; every following
 * call doubles the previous delay, saturating at @p max_ms.
 *
 * @param[in] previous_ms Delay returned by the previous call, or 0 for the first retry.
 * @param[in] initial_ms  First delay.
 * @param[in] max_ms      Ceiling for the delay.
 * @return Next delay in milliseconds, always within [@p initial_ms, @p max_ms].
 * @pre 0 < @p initial_ms <= @p max_ms (asserted).
 * @note Pure function: thread-safe, ISR-safe and non-blocking.
 */
uint32_t backoff_next_delay_ms(uint32_t previous_ms, uint32_t initial_ms, uint32_t max_ms);

#endif /* BACKOFF_H */
