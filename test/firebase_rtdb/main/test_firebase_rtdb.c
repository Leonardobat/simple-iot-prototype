#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fake_http_client.h"
#include "firebase_auth.h"
#include "firebase_rtdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "unity.h"

/* Real firebase_rtdb + firebase_auth (auth enabled): the token comes from the fake sign-in. */

#define DB_URL         "https://test-db.example.com"
#define PATH           "readings/dev/1"
#define STREAM_PATH    "devices/dev"
#define BODY           "{\"t\":1}"
#define TOKEN          "TEST-TOKEN"
#define SIGN_IN_OK     "{\"idToken\":\"" TOKEN "\",\"expiresIn\":\"3600\"}"
#define LONG_PATH_LEN  300U
#define STREAMER_STACK 4096U
#define STREAM_WAIT_MS 2000

static char s_long_path[LONG_PATH_LEN + 1U];
static char s_long_token[FIREBASE_ID_TOKEN_MAX_LEN];
static char s_long_response[FIREBASE_ID_TOKEN_MAX_LEN + 64U];
static SemaphoreHandle_t s_streamer_done;
static esp_err_t s_streamer_result;

static void ignore_event(const char *event, const char *data, void *ctx)
{
    (void)event;
    (void)data;
    (void)ctx;
}

void setUp(void)
{
    fake_http_client_reset();
    fake_http_client.post_response = SIGN_IN_OK;
    TEST_ASSERT_EQUAL_INT32(ESP_OK, firebase_rtdb_init());
}

void tearDown(void)
{
    firebase_rtdb_deinit();
}

TEST_CASE("test_firebase_rtdb_init_initializes_transport_once", "[firebase_rtdb]")
{
    TEST_ASSERT_EQUAL_UINT32(1U, fake_http_client.init_calls);
}

TEST_CASE("test_firebase_rtdb_deinit_releases_transport_and_reinit_works", "[firebase_rtdb]")
{
    firebase_rtdb_deinit();
    TEST_ASSERT_EQUAL_UINT32(1U, fake_http_client.deinit_calls);

    TEST_ASSERT_EQUAL_INT32(ESP_OK, firebase_rtdb_init());
    TEST_ASSERT_EQUAL_INT32(ESP_OK, firebase_rtdb_put(PATH, BODY));
}

TEST_CASE("test_firebase_rtdb_put_targets_json_url_with_auth_token", "[firebase_rtdb]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, firebase_rtdb_put(PATH, BODY));

    TEST_ASSERT_EQUAL_UINT32(1U, fake_http_client.put_calls);
    TEST_ASSERT_EQUAL_STRING(DB_URL "/" PATH ".json?auth=" TOKEN, fake_http_client.last_url);
    TEST_ASSERT_EQUAL_STRING(BODY, fake_http_client.last_body);
}

TEST_CASE("test_firebase_rtdb_consecutive_puts_sign_in_once", "[firebase_rtdb]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, firebase_rtdb_put(PATH, BODY));
    TEST_ASSERT_EQUAL_INT32(ESP_OK, firebase_rtdb_put(PATH, BODY));

    TEST_ASSERT_EQUAL_UINT32(1U, fake_http_client.post_calls);
    TEST_ASSERT_EQUAL_UINT32(2U, fake_http_client.put_calls);
}

TEST_CASE("test_firebase_rtdb_patch_targets_json_url_with_patch_transport", "[firebase_rtdb]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, firebase_rtdb_patch(PATH, BODY));

    TEST_ASSERT_EQUAL_UINT32(1U, fake_http_client.patch_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, fake_http_client.put_calls);
    TEST_ASSERT_EQUAL_STRING(DB_URL "/" PATH ".json?auth=" TOKEN, fake_http_client.last_url);
    TEST_ASSERT_EQUAL_STRING(BODY, fake_http_client.last_body);
}

TEST_CASE("test_firebase_rtdb_put_then_patch_sign_in_once", "[firebase_rtdb]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, firebase_rtdb_put(PATH, BODY));
    TEST_ASSERT_EQUAL_INT32(ESP_OK, firebase_rtdb_patch(PATH, BODY));

    TEST_ASSERT_EQUAL_UINT32(1U, fake_http_client.post_calls);
}

TEST_CASE("test_firebase_rtdb_patch_transport_error_propagates", "[firebase_rtdb]")
{
    fake_http_client.patch_result = ESP_ERR_TIMEOUT;

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_TIMEOUT, firebase_rtdb_patch(PATH, BODY));
}

TEST_CASE("test_firebase_rtdb_put_without_token_sends_nothing", "[firebase_rtdb]")
{
    fake_http_client.post_result = ESP_FAIL;

    TEST_ASSERT_EQUAL_INT32(ESP_FAIL, firebase_rtdb_put(PATH, BODY));
    TEST_ASSERT_EQUAL_UINT32(0U, fake_http_client.put_calls);
}

TEST_CASE("test_firebase_rtdb_put_transport_error_propagates", "[firebase_rtdb]")
{
    fake_http_client.put_result = ESP_ERR_TIMEOUT;

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_TIMEOUT, firebase_rtdb_put(PATH, BODY));
}

TEST_CASE("test_firebase_rtdb_put_too_long_path_returns_invalid_size", "[firebase_rtdb]")
{
    memset(s_long_path, 'p', LONG_PATH_LEN);
    s_long_path[LONG_PATH_LEN] = '\0';

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_SIZE, firebase_rtdb_put(s_long_path, BODY));
    TEST_ASSERT_EQUAL_UINT32(0U, fake_http_client.post_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, fake_http_client.put_calls);
}

TEST_CASE("test_firebase_rtdb_put_max_length_token_fits_url", "[firebase_rtdb]")
{
    memset(s_long_token, 't', sizeof(s_long_token) - 1U);
    s_long_token[sizeof(s_long_token) - 1U] = '\0';
    (void)snprintf(s_long_response, sizeof(s_long_response),
                   "{\"idToken\":\"%s\",\"expiresIn\":\"3600\"}", s_long_token);
    fake_http_client.post_response = s_long_response;

    TEST_ASSERT_EQUAL_INT32(ESP_OK, firebase_rtdb_put(PATH, BODY));

    const char *url = fake_http_client.last_url;
    TEST_ASSERT_EQUAL_STRING(s_long_token, &url[strlen(url) - strlen(s_long_token)]);
}

TEST_CASE("test_firebase_rtdb_stream_targets_authenticated_url", "[firebase_rtdb]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, firebase_rtdb_stream(STREAM_PATH, ignore_event, NULL));

    TEST_ASSERT_EQUAL_UINT32(1U, fake_http_client.stream_calls);
    TEST_ASSERT_EQUAL_STRING(DB_URL "/" STREAM_PATH ".json?auth=" TOKEN, fake_http_client.last_url);
}

TEST_CASE("test_firebase_rtdb_stream_without_token_does_not_connect", "[firebase_rtdb]")
{
    fake_http_client.post_result = ESP_FAIL;

    TEST_ASSERT_EQUAL_INT32(ESP_FAIL, firebase_rtdb_stream(STREAM_PATH, ignore_event, NULL));
    TEST_ASSERT_EQUAL_UINT32(0U, fake_http_client.stream_calls);
}

TEST_CASE("test_firebase_rtdb_stream_cancel_reaches_transport", "[firebase_rtdb]")
{
    firebase_rtdb_stream_cancel();

    TEST_ASSERT_EQUAL_UINT32(1U, fake_http_client.cancel_calls);
}

/* ---- Only one stream at a time ---- */

static void streamer_task(void *arg)
{
    (void)arg; /* No argument: results go to file-scope variables. */
    s_streamer_result = firebase_rtdb_stream(STREAM_PATH, ignore_event, NULL);
    (void)xSemaphoreGive(s_streamer_done);
    vTaskDelete(NULL);
}

TEST_CASE("test_firebase_rtdb_second_concurrent_stream_returns_invalid_state", "[firebase_rtdb]")
{
    fake_http_client.stream_blocks = true;
    s_streamer_done = xSemaphoreCreateBinary();
    TEST_ASSERT_EQUAL_INT32(pdPASS, xTaskCreate(streamer_task, "streamer", STREAMER_STACK, NULL,
                                                tskIDLE_PRIORITY + 2U, NULL));
    TEST_ASSERT_TRUE(fake_http_client_emit(NULL, 0U)); /* The first stream is open. */

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_STATE,
                            firebase_rtdb_stream(STREAM_PATH, ignore_event, NULL));

    firebase_rtdb_stream_cancel();
    TEST_ASSERT_TRUE(xSemaphoreTake(s_streamer_done, pdMS_TO_TICKS(STREAM_WAIT_MS)));
    vSemaphoreDelete(s_streamer_done);
    TEST_ASSERT_EQUAL_INT32(ESP_OK, s_streamer_result);
}
