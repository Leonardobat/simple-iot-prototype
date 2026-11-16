#include "mq135.h"

#include <math.h>

#include "sensor_provider_config.h"

float mq135_resistance_ohms(int32_t millivolts)
{
    const float module_mv = (float)millivolts * SENSOR_MQ135_DIVIDER_RATIO;
    if ((module_mv <= 0.0f) || (module_mv >= SENSOR_MQ135_SUPPLY_MV)) {
        return NAN; /* No signal, or Rs = 0: wiring fault. */
    }
    return SENSOR_MQ135_LOAD_OHMS * ((SENSOR_MQ135_SUPPLY_MV / module_mv) - 1.0f);
}

float mq135_ppm_from_mv(int32_t millivolts)
{
    const float rs_ohms = mq135_resistance_ohms(millivolts);
    if (isnan(rs_ohms)) {
        return NAN; /* fminf() below would turn a NaN into the maximum. */
    }
    const float ppm =
        SENSOR_MQ135_CURVE_A * powf(rs_ohms / SENSOR_MQ135_R0_OHMS, -SENSOR_MQ135_CURVE_B);
    return fminf(ppm, SENSOR_MQ135_MAX_PPM);
}
