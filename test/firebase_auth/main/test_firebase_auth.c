#include <stdint.h>
#include <string.h>

#include "fake_http_client.h"
#include "firebase_auth.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "unity.h"

#define STRINGIFY_(x) #x
#define STRINGIFY(x)  STRINGIFY_(x)

#define SIGN_IN_URL                                                                                \
    "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=test-api-key"
#define TOKEN "ID-TOKEN-1"
#define VALID_RESPONSE                                                                             \
    "{\"kind\":\"x\",\"idToken\":\"" TOKEN "\",\"refreshToken\":\"R\",\"expiresIn\":\"3600\"}"
/* A token that is already inside the renewal margin when it is received. */
#define EXPIRING_RESPONSE                                                                          \
    "{\"idToken\":\"" TOKEN "\",\"expiresIn\":\"" STRINGIFY(FIREBASE_AUTH_RENEW_MARGIN_S) "\"}"

#define CONCURRENT_CALLERS     2U
#define SLOW_SIGN_IN_MS        200U
#define CALLER_STACK_SIZE      4096U
#define CALLER_DONE_TIMEOUT_MS 2000

static char s_token[FIREBASE_ID_TOKEN_MAX_LEN];

void setUp(void)
{
    fake_http_client_reset();
    fake_http_client.post_response = VALID_RESPONSE;
    TEST_ASSERT_EQUAL_INT32(ESP_OK, firebase_auth_init());
    s_token[0] = '\0';
}

void tearDown(void)
{
    firebase_auth_deinit();
}

static esp_err_t get_token(void)
{
    return firebase_auth_get_token(s_token, sizeof(s_token));
}

static void expect_invalid_response(const char *response)
{
    fake_http_client.post_response = response;
    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_RESPONSE, get_token());
}

TEST_CASE("test_firebase_auth_email_configured_is_enabled", "[firebase_auth]")
{
    TEST_ASSERT_TRUE(firebase_auth_is_enabled());
}

TEST_CASE("test_firebase_auth_first_call_signs_in_with_configured_credentials", "[firebase_auth]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, get_token());

    TEST_ASSERT_EQUAL_UINT32(1U, fake_http_client.post_calls);
    TEST_ASSERT_EQUAL_STRING(SIGN_IN_URL, fake_http_client.last_url);
    TEST_ASSERT_EQUAL_STRING(
        "{\"email\":\"device@test.com\",\"password\":\"secret\",\"returnSecureToken\":true}",
        fake_http_client.last_body);
    TEST_ASSERT_EQUAL_STRING(TOKEN, s_token);
}

TEST_CASE("test_firebase_auth_valid_token_is_reused", "[firebase_auth]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, get_token());
    TEST_ASSERT_EQUAL_INT32(ESP_OK, get_token());

    TEST_ASSERT_EQUAL_UINT32(1U, fake_http_client.post_calls);
    TEST_ASSERT_EQUAL_STRING(TOKEN, s_token);
}

TEST_CASE("test_firebase_auth_token_within_renew_margin_signs_in_again", "[firebase_auth]")
{
    fake_http_client.post_response = EXPIRING_RESPONSE;

    TEST_ASSERT_EQUAL_INT32(ESP_OK, get_token());
    TEST_ASSERT_EQUAL_INT32(ESP_OK, get_token());

    TEST_ASSERT_EQUAL_UINT32(2U, fake_http_client.post_calls);
}

TEST_CASE("test_firebase_auth_deinit_then_init_discards_token", "[firebase_auth]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, get_token());
    firebase_auth_deinit();
    TEST_ASSERT_EQUAL_INT32(ESP_OK, firebase_auth_init());
    TEST_ASSERT_EQUAL_INT32(ESP_OK, get_token());

    TEST_ASSERT_EQUAL_UINT32(2U, fake_http_client.post_calls);
}

TEST_CASE("test_firebase_auth_sign_in_error_propagates_and_next_call_retries", "[firebase_auth]")
{
    fake_http_client.post_result = ESP_FAIL;
    fake_http_client.post_response = "{\"error\":{\"message\":\"INVALID_LOGIN_CREDENTIALS\"}}";
    TEST_ASSERT_EQUAL_INT32(ESP_FAIL, get_token());

    fake_http_client.post_result = ESP_OK;
    fake_http_client.post_response = VALID_RESPONSE;
    TEST_ASSERT_EQUAL_INT32(ESP_OK, get_token());

    TEST_ASSERT_EQUAL_UINT32(2U, fake_http_client.post_calls);
    TEST_ASSERT_EQUAL_STRING(TOKEN, s_token);
}

TEST_CASE("test_firebase_auth_response_without_id_token_is_invalid_and_retried", "[firebase_auth]")
{
    expect_invalid_response("{\"expiresIn\":\"3600\"}");
    expect_invalid_response("{\"expiresIn\":\"3600\"}");

    TEST_ASSERT_EQUAL_UINT32(2U, fake_http_client.post_calls);
}

TEST_CASE("test_firebase_auth_response_without_expires_in_is_invalid", "[firebase_auth]")
{
    expect_invalid_response("{\"idToken\":\"" TOKEN "\"}");
}

TEST_CASE("test_firebase_auth_out_of_range_expires_in_is_invalid", "[firebase_auth]")
{
    expect_invalid_response("{\"idToken\":\"" TOKEN "\",\"expiresIn\":\"abc\"}");
    expect_invalid_response("{\"idToken\":\"" TOKEN "\",\"expiresIn\":\"0\"}");
    expect_invalid_response("{\"idToken\":\"" TOKEN "\",\"expiresIn\":\"-5\"}");
    expect_invalid_response("{\"idToken\":\"" TOKEN "\",\"expiresIn\":\"86401\"}");
    expect_invalid_response("{\"idToken\":\"" TOKEN "\",\"expiresIn\":\"60s\"}");
}

TEST_CASE("test_firebase_auth_malformed_response_is_invalid", "[firebase_auth]")
{
    expect_invalid_response("<html>502 Bad Gateway</html>");
}

TEST_CASE("test_firebase_auth_response_with_too_many_fields_is_invalid", "[firebase_auth]")
{
    expect_invalid_response("{\"a\":1,\"b\":2,\"c\":3,\"d\":4,\"e\":5,\"f\":6,\"g\":7,\"h\":8,"
                            "\"i\":9,\"j\":10,\"k\":11,\"l\":12,\"m\":13,\"n\":14,\"o\":15,"
                            "\"p\":16,\"idToken\":\"" TOKEN "\",\"expiresIn\":\"3600\"}");
}

TEST_CASE("test_firebase_auth_small_caller_buffer_returns_invalid_size", "[firebase_auth]")
{
    char small[4] = {0};

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_SIZE, firebase_auth_get_token(small, sizeof(small)));
}

/* ---- Concurrency: the lock must serialize callers so only one signs in ---- */

typedef struct {
    SemaphoreHandle_t done;
    esp_err_t result;
    char token[FIREBASE_ID_TOKEN_MAX_LEN];
} caller_t;

static caller_t s_callers[CONCURRENT_CALLERS];

static void caller_task(void *arg)
{
    caller_t *caller = arg;
    caller->result = firebase_auth_get_token(caller->token, sizeof(caller->token));
    (void)xSemaphoreGive(caller->done);
    vTaskDelete(NULL);
}

static void start_callers(void)
{
    for (uint32_t i = 0U; i < CONCURRENT_CALLERS; i++) {
        s_callers[i].done = xSemaphoreCreateBinary();
        s_callers[i].result = ESP_FAIL;
        TEST_ASSERT_EQUAL_INT32(pdPASS, xTaskCreate(caller_task, "auth_caller", CALLER_STACK_SIZE,
                                                    &s_callers[i], tskIDLE_PRIORITY + 2U, NULL));
    }
}

TEST_CASE("test_firebase_auth_concurrent_callers_share_one_sign_in", "[firebase_auth]")
{
    fake_http_client.post_delay_ms = SLOW_SIGN_IN_MS;

    start_callers();
    for (uint32_t i = 0U; i < CONCURRENT_CALLERS; i++) {
        TEST_ASSERT_TRUE(xSemaphoreTake(s_callers[i].done, pdMS_TO_TICKS(CALLER_DONE_TIMEOUT_MS)));
        vSemaphoreDelete(s_callers[i].done);
        TEST_ASSERT_EQUAL_INT32(ESP_OK, s_callers[i].result);
        TEST_ASSERT_EQUAL_STRING(TOKEN, s_callers[i].token);
    }

    TEST_ASSERT_EQUAL_UINT32(1U, fake_http_client.post_calls);
}
