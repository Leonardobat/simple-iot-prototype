#include "gpio_output.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "gpio_output";

esp_err_t gpio_output_init(int32_t gpio, bool pull_up, bool high)
{
    ESP_RETURN_ON_FALSE(GPIO_IS_VALID_OUTPUT_GPIO(gpio), ESP_ERR_INVALID_ARG, TAG,
                        "GPIO%d cannot be an output", (int)gpio);
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << (uint32_t)gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = pull_up ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_set_level((gpio_num_t)gpio, high ? 1U : 0U), TAG, "GPIO%d level",
                        (int)gpio);
    return gpio_config(&config);
}

void gpio_output_deinit(int32_t gpio)
{
    if (gpio_reset_pin((gpio_num_t)gpio) != ESP_OK) {
        ESP_LOGW(TAG, "GPIO%d not reset", (int)gpio);
    }
}

esp_err_t gpio_output_set_level(int32_t gpio, bool high)
{
    return gpio_set_level((gpio_num_t)gpio, high ? 1U : 0U);
}
