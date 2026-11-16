#include <string.h>

#include "http_client.h"
#include "unity.h"

/* Only checks that never reach the network: QEMU has no Wi-Fi (see test/integration). */

static char s_long_url[HTTP_CLIENT_URL_MAX_LEN + 1U];
static char s_response[64];

static void ignore_event(const char *event, const char *data, void *ctx)
{
    (void)event;
    (void)data;
    (void)ctx;
}

/* An https URL exactly HTTP_CLIENT_URL_MAX_LEN characters long: one too many. */
static const char *too_long_url(void)
{
    memset(s_long_url, 'a', HTTP_CLIENT_URL_MAX_LEN);
    memcpy(s_long_url, "https://", strlen("https://"));
    s_long_url[HTTP_CLIENT_URL_MAX_LEN] = '\0';
    return s_long_url;
}

void setUp(void)
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, http_client_init());
}

void tearDown(void)
{
    http_client_deinit();
}

TEST_CASE("test_http_client_deinit_then_init_succeeds", "[http_client]")
{
    http_client_deinit();

    TEST_ASSERT_EQUAL_INT32(ESP_OK, http_client_init());
}

TEST_CASE("test_http_client_put_too_long_url_returns_invalid_size", "[http_client]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_SIZE, http_client_put(too_long_url(), "{}"));
}

TEST_CASE("test_http_client_patch_too_long_url_returns_invalid_size", "[http_client]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_SIZE, http_client_patch(too_long_url(), "{}"));
}

TEST_CASE("test_http_client_post_too_long_url_returns_invalid_size_and_empty_response",
          "[http_client]")
{
    memset(s_response, 'x', sizeof(s_response));

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_SIZE,
                            http_client_post(too_long_url(), "{}", s_response, sizeof(s_response)));
    TEST_ASSERT_EQUAL_STRING("", s_response);
}

TEST_CASE("test_http_client_stream_too_long_url_returns_invalid_size", "[http_client]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_SIZE,
                            http_client_stream(too_long_url(), ignore_event, NULL));
}

TEST_CASE("test_http_client_cancel_without_stream_is_harmless", "[http_client]")
{
    http_client_stream_cancel();

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_SIZE, http_client_put(too_long_url(), "{}"));
}
