#include "ldr.h"

#include <math.h>
#include <stdbool.h>

#include "sensor_provider_config.h"

float ldr_lux_from_mv(int32_t millivolts)
{
    if ((millivolts < 0) || (millivolts > SENSOR_LDR_SUPPLY_MV)) {
        return NAN;
    }
    const float pin_mv = (float)millivolts;
    const float supply_mv = (float)SENSOR_LDR_SUPPLY_MV;
    const float ldr_mv = SENSOR_LDR_TO_SUPPLY ? (supply_mv - pin_mv) : pin_mv;
    const float fixed_mv = supply_mv - ldr_mv;
    if (fixed_mv <= 0.0f) {
        return 0.0f; /* No current through the divider: LDR at its dark resistance. */
    }
    if (ldr_mv <= 0.0f) {
        return SENSOR_LDR_MAX_LUX; /* LDR resistance near zero: saturated. */
    }
    const float ldr_ohms = SENSOR_LDR_FIXED_OHMS * ldr_mv / fixed_mv;
    const float lux =
        SENSOR_LDR_REF_LUX * powf(SENSOR_LDR_R10_OHMS / ldr_ohms, 1.0f / SENSOR_LDR_GAMMA);
    return fminf(lux, SENSOR_LDR_MAX_LUX);
}
