#include "rgb_led.h"

#include <assert.h>
#include <stddef.h>

#include "esp_check.h"
#include "esp_log.h"
#include "led_strip.h"
#include "rgb_led_config.h"
#include "sdkconfig.h"

static const char *TAG = "rgb_led";

static led_strip_handle_t s_strip;

esp_err_t rgb_led_init(void)
{
    assert(s_strip == NULL);

    const led_strip_config_t strip_config = {
        .strip_gpio_num = CONFIG_RGB_LED_GPIO,
        .max_leds = RGB_LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    const led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RGB_LED_RMT_RESOLUTION_HZ,
    };
    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip), TAG,
                        "LED on GPIO%d not created", CONFIG_RGB_LED_GPIO);

    const esp_err_t err = led_strip_clear(s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LED not switched off (%s)", esp_err_to_name(err));
        rgb_led_deinit();
        return err;
    }
    ESP_LOGI(TAG, "RGB LED on GPIO%d", CONFIG_RGB_LED_GPIO);
    return ESP_OK;
}

void rgb_led_deinit(void)
{
    if (s_strip == NULL) {
        return;
    }
    if (led_strip_clear(s_strip) != ESP_OK) {
        ESP_LOGW(TAG, "LED not switched off");
    }
    if (led_strip_del(s_strip) != ESP_OK) {
        ESP_LOGW(TAG, "LED strip not released");
    }
    s_strip = NULL;
}

esp_err_t rgb_led_set(uint8_t red, uint8_t green, uint8_t blue)
{
    assert(s_strip != NULL);

    ESP_RETURN_ON_ERROR(led_strip_set_pixel(s_strip, RGB_LED_INDEX, red, green, blue), TAG,
                        "Colour not set");
    return led_strip_refresh(s_strip);
}
