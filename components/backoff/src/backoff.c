#include "backoff.h"

#include <assert.h>

uint32_t backoff_next_delay_ms(uint32_t previous_ms, uint32_t initial_ms, uint32_t max_ms)
{
    assert((initial_ms > 0U) && (initial_ms <= max_ms));

    if (previous_ms < initial_ms) {
        return initial_ms;
    }
    /* Compare against half the ceiling instead of doubling first, so the product cannot overflow.
     */
    if (previous_ms > (max_ms / 2U)) {
        return max_ms;
    }
    return previous_ms * 2U;
}
