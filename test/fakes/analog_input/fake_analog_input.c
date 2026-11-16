#include "fake_analog_input.h"

#include <assert.h>
#include <string.h>

#define FAKE_DEFAULT_MV 1650

struct analog_input_s {
    bool in_use;
    int32_t gpio;
};

fake_analog_input_t fake_analog_input;
static struct analog_input_s s_inputs[ANALOG_INPUT_MAX_INPUTS];

void fake_analog_input_reset(void)
{
    memset(&fake_analog_input, 0, sizeof(fake_analog_input));
    memset(s_inputs, 0, sizeof(s_inputs));
    fake_analog_input.init_result = ESP_OK;
    fake_analog_input.init_fail_gpio = -1;
    for (uint32_t i = 0U; i < FAKE_ANALOG_INPUT_GPIO_COUNT; i++) {
        fake_analog_input.channel[i].millivolts = FAKE_DEFAULT_MV;
        fake_analog_input.channel[i].read_result = ESP_OK;
    }
}

esp_err_t analog_input_init(int32_t gpio, analog_input_handle_t *out_handle)
{
    fake_analog_input.init_calls++;
    if ((gpio < 0) || ((uint32_t)gpio >= FAKE_ANALOG_INPUT_GPIO_COUNT)) {
        return ESP_ERR_INVALID_ARG;
    }
    const bool fails =
        (fake_analog_input.init_fail_gpio < 0) || (fake_analog_input.init_fail_gpio == gpio);
    if (fails && (fake_analog_input.init_result != ESP_OK)) {
        return fake_analog_input.init_result;
    }
    for (uint32_t i = 0U; i < ANALOG_INPUT_MAX_INPUTS; i++) {
        if (!s_inputs[i].in_use) {
            s_inputs[i] = (struct analog_input_s){.in_use = true, .gpio = gpio};
            fake_analog_input.open_inputs++;
            *out_handle = &s_inputs[i];
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

void analog_input_deinit(analog_input_handle_t handle)
{
    if (handle == NULL) {
        return;
    }
    fake_analog_input.deinit_calls++;
    fake_analog_input.open_inputs--;
    handle->in_use = false;
}

esp_err_t analog_input_read_mv(analog_input_handle_t handle, int32_t *out_mv)
{
    assert((handle != NULL) && handle->in_use);
    fake_analog_channel_t *channel = &fake_analog_input.channel[handle->gpio];
    channel->read_calls++;
    if (channel->read_result == ESP_OK) {
        *out_mv = channel->millivolts;
    }
    return channel->read_result;
}

bool analog_input_is_calibrated(analog_input_handle_t handle)
{
    (void)handle; /* The fake always reports a calibrated input. */
    return true;
}
