#include "analog_input.h"

#include <assert.h>
#include <stddef.h>

#include "analog_input_config.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"

struct analog_input_s {
    bool in_use;
    adc_channel_t channel;
    adc_cali_handle_t cali; /* NULL without calibration eFuses. */
};

static const char *TAG = "analog_input";

static struct analog_input_s s_inputs[ANALOG_INPUT_MAX_INPUTS];
static adc_oneshot_unit_handle_t s_unit; /* ADC1, shared by every open input. */

static struct analog_input_s *find_free_input(void)
{
    for (uint32_t i = 0U; i < ANALOG_INPUT_MAX_INPUTS; i++) {
        if (!s_inputs[i].in_use) {
            return &s_inputs[i];
        }
    }
    return NULL;
}

static bool any_input_open(void)
{
    for (uint32_t i = 0U; i < ANALOG_INPUT_MAX_INPUTS; i++) {
        if (s_inputs[i].in_use) {
            return true;
        }
    }
    return false;
}

static void release_unit_if_unused(void)
{
    if ((s_unit == NULL) || any_input_open()) {
        return;
    }
    if (adc_oneshot_del_unit(s_unit) != ESP_OK) {
        ESP_LOGW(TAG, "ADC unit not released");
    }
    s_unit = NULL;
}

static esp_err_t claim_unit(void)
{
    if (s_unit != NULL) {
        return ESP_OK;
    }
    const adc_oneshot_unit_init_cfg_t unit_config = {.unit_id = ADC_UNIT_1};
    return adc_oneshot_new_unit(&unit_config, &s_unit);
}

/* Curve fitting from the eFuse calibration; without it, readings use the linear fallback. */
static void create_calibration(struct analog_input_s *input)
{
    const adc_cali_curve_fitting_config_t config = {
        .unit_id = ADC_UNIT_1,
        .chan = input->channel,
        .atten = ANALOG_INPUT_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    const esp_err_t err = adc_cali_create_scheme_curve_fitting(&config, &input->cali);
    if (err != ESP_OK) {
        input->cali = NULL;
        ESP_LOGW(TAG, "No ADC calibration (%s): readings are approximate", esp_err_to_name(err));
    }
}

static esp_err_t channel_of(int32_t gpio, adc_channel_t *out_channel)
{
    adc_unit_t unit = ADC_UNIT_1;
    ESP_RETURN_ON_ERROR(adc_oneshot_io_to_channel((int)gpio, &unit, out_channel), TAG,
                        "GPIO%d has no ADC channel", (int)gpio);
    ESP_RETURN_ON_FALSE(unit == ADC_UNIT_1, ESP_ERR_NOT_SUPPORTED, TAG,
                        "GPIO%d is on ADC2, unusable with Wi-Fi", (int)gpio);
    return ESP_OK;
}

static esp_err_t configure_channel(adc_channel_t channel)
{
    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = ANALOG_INPUT_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(claim_unit(), TAG, "ADC unit not claimed");
    return adc_oneshot_config_channel(s_unit, channel, &channel_config);
}

esp_err_t analog_input_init(int32_t gpio, analog_input_handle_t *out_handle)
{
    assert(out_handle != NULL);

    struct analog_input_s *input = find_free_input();
    ESP_RETURN_ON_FALSE(input != NULL, ESP_ERR_NO_MEM, TAG, "No free analog input");

    adc_channel_t channel = ADC_CHANNEL_0;
    ESP_RETURN_ON_ERROR(channel_of(gpio, &channel), TAG, "Invalid analog input");
    const esp_err_t err = configure_channel(channel);
    if (err != ESP_OK) {
        release_unit_if_unused();
        ESP_LOGE(TAG, "GPIO%d channel not configured (%s)", (int)gpio, esp_err_to_name(err));
        return err;
    }
    input->in_use = true;
    input->channel = channel;
    create_calibration(input);
    ESP_LOGI(TAG, "GPIO%d on ADC1 channel %d (%s)", (int)gpio, (int)channel,
             (input->cali != NULL) ? "calibrated" : "uncalibrated");
    *out_handle = input;
    return ESP_OK;
}

void analog_input_deinit(analog_input_handle_t handle)
{
    if (handle == NULL) {
        return;
    }
    if ((handle->cali != NULL) && (adc_cali_delete_scheme_curve_fitting(handle->cali) != ESP_OK)) {
        ESP_LOGW(TAG, "Calibration not released");
    }
    handle->cali = NULL;
    handle->in_use = false;
    release_unit_if_unused();
}

esp_err_t analog_input_read_mv(analog_input_handle_t handle, int32_t *out_mv)
{
    assert((handle != NULL) && handle->in_use);
    assert(out_mv != NULL);

    int value = 0;
    if (handle->cali != NULL) {
        ESP_RETURN_ON_ERROR(
            adc_oneshot_get_calibrated_result(s_unit, handle->cali, handle->channel, &value), TAG,
            "Calibrated read failed");
        *out_mv = (int32_t)value;
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(adc_oneshot_read(s_unit, handle->channel, &value), TAG, "Read failed");
    *out_mv = ((int32_t)value * ANALOG_INPUT_UNCALIBRATED_MAX_MV) / ANALOG_INPUT_RAW_FULL_SCALE;
    return ESP_OK;
}

bool analog_input_is_calibrated(analog_input_handle_t handle)
{
    assert((handle != NULL) && handle->in_use);
    return handle->cali != NULL;
}
