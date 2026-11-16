#include "lm35.h"

#include <math.h>

#include "sensor_provider_config.h"

float lm35_celsius_from_mv(int32_t millivolts)
{
    if ((millivolts < 0) || (millivolts > SENSOR_LM35_MAX_MV)) {
        return NAN;
    }
    return (float)millivolts / SENSOR_LM35_MV_PER_C;
}
