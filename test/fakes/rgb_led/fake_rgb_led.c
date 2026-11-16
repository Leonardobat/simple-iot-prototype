#include "fake_rgb_led.h"

#include <string.h>

fake_rgb_led_t fake_rgb_led;

static void show(uint8_t red, uint8_t green, uint8_t blue)
{
    fake_rgb_led.red = red;
    fake_rgb_led.green = green;
    fake_rgb_led.blue = blue;
}

void fake_rgb_led_reset(void)
{
    memset(&fake_rgb_led, 0, sizeof(fake_rgb_led));
    fake_rgb_led.init_result = ESP_OK;
    fake_rgb_led.set_result = ESP_OK;
}

esp_err_t rgb_led_init(void)
{
    fake_rgb_led.init_calls++;
    show(0U, 0U, 0U);
    return fake_rgb_led.init_result;
}

void rgb_led_deinit(void)
{
    fake_rgb_led.deinit_calls++;
    show(0U, 0U, 0U);
}

esp_err_t rgb_led_set(uint8_t red, uint8_t green, uint8_t blue)
{
    fake_rgb_led.set_calls++;
    if (fake_rgb_led.set_result == ESP_OK) {
        show(red, green, blue);
    }
    return fake_rgb_led.set_result;
}
