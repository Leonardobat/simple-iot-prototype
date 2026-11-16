#include "firebase_auth.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "firebase_auth_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "http_client.h"
#include "json_parser.h"
#include "utils.h"

#define RENEW_MARGIN_US ((int64_t)FIREBASE_AUTH_RENEW_MARGIN_S * FIREBASE_AUTH_US_PER_S)

static const char *TAG = "firebase_auth";

/* Every variable below is guarded by s_lock. */
static SemaphoreHandle_t s_lock;
static char s_id_token[FIREBASE_ID_TOKEN_MAX_LEN];
static int64_t s_expires_at_us;
static char s_response[FIREBASE_AUTH_RESPONSE_MAX_LEN];
static json_tok_t s_json_tokens[FIREBASE_AUTH_JSON_MAX_TOKENS];

static bool is_json_safe(const char *value)
{
    return strpbrk(value, "\"\\") == NULL;
}

static esp_err_t check_credentials_config(void)
{
    ESP_RETURN_ON_FALSE(strlen(CONFIG_FIREBASE_API_KEY) > 0U, ESP_ERR_INVALID_STATE, TAG,
                        "CONFIG_FIREBASE_API_KEY is not set");
    ESP_RETURN_ON_FALSE(strlen(CONFIG_FIREBASE_USER_PASSWORD) > 0U, ESP_ERR_INVALID_STATE, TAG,
                        "CONFIG_FIREBASE_USER_PASSWORD is not set");
    ESP_RETURN_ON_FALSE(
        is_json_safe(CONFIG_FIREBASE_USER_EMAIL) && is_json_safe(CONFIG_FIREBASE_USER_PASSWORD),
        ESP_ERR_INVALID_ARG, TAG, "Email and password must not contain '\"' or '\\'");
    return ESP_OK;
}

static esp_err_t build_sign_in_body(char *body, size_t size)
{
    ESP_RETURN_ON_ERROR(check_credentials_config(), TAG, "Invalid credentials config");

    return utils_snprintf(body, size,
                          "{\"email\":\"%s\",\"password\":\"%s\",\"returnSecureToken\":true}",
                          CONFIG_FIREBASE_USER_EMAIL, CONFIG_FIREBASE_USER_PASSWORD);
}

/* "expiresIn" comes from the network: accept only a positive decimal within range. */
static esp_err_t parse_expires_in_s(const char *text, int64_t *out_seconds)
{
    char *end = NULL;
    const int64_t seconds = (int64_t)strtoll(text, &end, 10);
    ESP_RETURN_ON_FALSE((end != text) && (*end == '\0') && (seconds > 0) &&
                            (seconds <= FIREBASE_AUTH_EXPIRES_IN_MAX_S),
                        ESP_ERR_INVALID_RESPONSE, TAG, "Invalid expiresIn '%s'", text);
    *out_seconds = seconds;
    return ESP_OK;
}

static esp_err_t read_token_fields(jparse_ctx_t *jctx, int64_t *out_expires_in_s)
{
    char expires_in[FIREBASE_AUTH_EXPIRES_IN_MAX_LEN] = {0};
    const bool found =
        (json_obj_get_string(jctx, "idToken", s_id_token, (int)sizeof(s_id_token)) == OS_SUCCESS) &&
        (json_obj_get_string(jctx, "expiresIn", expires_in, (int)sizeof(expires_in)) == OS_SUCCESS);
    ESP_RETURN_ON_FALSE(found, ESP_ERR_INVALID_RESPONSE, TAG,
                        "Sign-in response without idToken/expiresIn");
    return parse_expires_in_s(expires_in, out_expires_in_s);
}

static esp_err_t store_token(const char *response)
{
    jparse_ctx_t jctx = {0};
    ESP_RETURN_ON_FALSE(json_parse_start_static(&jctx, response, (int)strlen(response),
                                                s_json_tokens,
                                                FIREBASE_AUTH_JSON_MAX_TOKENS) == OS_SUCCESS,
                        ESP_ERR_INVALID_RESPONSE, TAG, "Invalid sign-in response");

    int64_t expires_in_s = 0;
    const esp_err_t err = read_token_fields(&jctx, &expires_in_s);
    (void)json_parse_end_static(&jctx); /* Only resets the context; cannot fail. */
    ESP_RETURN_ON_ERROR(err, TAG, "Token not stored");

    s_expires_at_us = esp_timer_get_time() + (expires_in_s * FIREBASE_AUTH_US_PER_S);
    return ESP_OK;
}

static esp_err_t sign_in(void)
{
    s_expires_at_us = 0;

    char body[FIREBASE_AUTH_REQUEST_BODY_MAX_LEN] = {0};
    ESP_RETURN_ON_ERROR(build_sign_in_body(body, sizeof(body)), TAG, "Sign-in body not built");

    esp_err_t err =
        http_client_post(FIREBASE_AUTH_SIGN_IN_URL, body, s_response, sizeof(s_response));
    memset(body, 0, sizeof(body)); /* Holds the password. */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Sign-in failed (%s): %s", esp_err_to_name(err), s_response);
        return err;
    }
    err = store_token(s_response);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Signed in as %s", CONFIG_FIREBASE_USER_EMAIL);
    }
    return err;
}

static bool token_is_fresh(void)
{
    return esp_timer_get_time() < (s_expires_at_us - RENEW_MARGIN_US);
}

esp_err_t firebase_auth_init(void)
{
    assert(s_lock == NULL);

    s_expires_at_us = 0;
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock != NULL, ESP_ERR_NO_MEM, TAG, "Mutex alloc failed");
    return ESP_OK;
}

void firebase_auth_deinit(void)
{
    if (s_lock != NULL) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
    }
    s_expires_at_us = 0;
    memset(s_id_token, 0, sizeof(s_id_token));
}

bool firebase_auth_is_enabled(void)
{
    return strlen(CONFIG_FIREBASE_USER_EMAIL) > 0U;
}

esp_err_t firebase_auth_get_token(char *token, size_t size)
{
    assert(s_lock != NULL);
    assert(token != NULL);
    ESP_RETURN_ON_FALSE(xSemaphoreTake(s_lock, pdMS_TO_TICKS(FIREBASE_AUTH_LOCK_TIMEOUT_MS)) ==
                            pdTRUE,
                        ESP_ERR_TIMEOUT, TAG, "Token lock busy");

    esp_err_t err = token_is_fresh() ? ESP_OK : sign_in();
    if ((err == ESP_OK) && (strlcpy(token, s_id_token, size) >= size)) {
        err = ESP_ERR_INVALID_SIZE;
    }
    (void)xSemaphoreGive(s_lock); /* Cannot fail: taken above by this task. */
    return err;
}
