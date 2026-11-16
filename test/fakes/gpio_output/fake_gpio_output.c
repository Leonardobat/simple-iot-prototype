#include "fake_gpio_output.h"

#include <assert.h>
#include <string.h>

fake_gpio_output_t fake_gpio_output;

static uint32_t index_of(int32_t gpio)
{
    assert((gpio >= 0) && ((uint32_t)gpio < FAKE_GPIO_COUNT));
    return (uint32_t)gpio;
}

void fake_gpio_output_reset(void)
{
    memset(&fake_gpio_output, 0, sizeof(fake_gpio_output));
    fake_gpio_output.init_result = ESP_OK;
    fake_gpio_output.set_result = ESP_OK;
}

esp_err_t gpio_output_init(int32_t gpio, bool pull_up, bool high)
{
    const uint32_t index = index_of(gpio);
    fake_gpio_output.init_calls++;
    fake_gpio_output.pull_up[index] = pull_up;
    fake_gpio_output.high[index] = high;
    fake_gpio_output.configured[index] = fake_gpio_output.init_result == ESP_OK;
    return fake_gpio_output.init_result;
}

void gpio_output_deinit(int32_t gpio)
{
    const uint32_t index = index_of(gpio);
    fake_gpio_output.deinit_calls++;
    fake_gpio_output.configured[index] = false;
    fake_gpio_output.high[index] = false;
}

esp_err_t gpio_output_set_level(int32_t gpio, bool high)
{
    const uint32_t index = index_of(gpio);
    fake_gpio_output.set_calls++;
    if (fake_gpio_output.set_result == ESP_OK) {
        fake_gpio_output.high[index] = high;
        if (fake_gpio_output.log_len < FAKE_GPIO_LOG_SIZE) {
            fake_gpio_output.log[fake_gpio_output.log_len++] = (fake_gpio_write_t){gpio, high};
        }
    }
    return fake_gpio_output.set_result;
}
