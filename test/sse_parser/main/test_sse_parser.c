#include <stdint.h>
#include <string.h>

#include "sse_parser.h"
#include "unity.h"

#define MAX_RECORDED_EVENTS 4U
#define DATA_PREFIX         "data: "
#define DATA_PREFIX_LEN     (sizeof(DATA_PREFIX) - 1U)

typedef struct {
    char event[SSE_PARSER_EVENT_NAME_MAX_LEN];
    char data[SSE_PARSER_LINE_MAX_LEN];
} recorded_event_t;

static sse_parser_handle_t s_parser;
static recorded_event_t s_events[MAX_RECORDED_EVENTS];
static uint32_t s_event_count;
static char s_long_value[SSE_PARSER_LINE_MAX_LEN + 1U];
static void *s_captured_ctx;

static void record_event(const char *event, const char *data, void *ctx)
{
    (void)ctx; /* Not used by these tests. */
    TEST_ASSERT_LESS_THAN_UINT32(MAX_RECORDED_EVENTS, s_event_count);
    (void)strlcpy(s_events[s_event_count].event, event, sizeof(s_events[0].event));
    (void)strlcpy(s_events[s_event_count].data, data, sizeof(s_events[0].data));
    s_event_count++;
}

static void capture_ctx(const char *event, const char *data, void *ctx)
{
    (void)event;
    (void)data;
    s_captured_ctx = ctx;
}

static void feed(const char *text)
{
    sse_parser_feed(s_parser, text, strlen(text));
}

static void assert_event(uint32_t index, const char *event, const char *data)
{
    TEST_ASSERT_EQUAL_STRING(event, s_events[index].event);
    TEST_ASSERT_EQUAL_STRING(data, s_events[index].data);
}

/* Builds a "data: xxx..." value of `len` characters into s_long_value. */
static const char *long_value(size_t len)
{
    memset(s_long_value, 'x', len);
    s_long_value[len] = '\0';
    return s_long_value;
}

void setUp(void)
{
    s_event_count = 0U;
    TEST_ASSERT_EQUAL_INT32(ESP_OK, sse_parser_init(record_event, NULL, &s_parser));
}

void tearDown(void)
{
    sse_parser_deinit(s_parser);
}

TEST_CASE("test_sse_parser_blank_line_dispatches_pending_event", "[sse_parser]")
{
    feed("event: put\ndata: {\"path\":\"/\",\"data\":1}\n");
    TEST_ASSERT_EQUAL_UINT32(0U, s_event_count);

    feed("\n");
    TEST_ASSERT_EQUAL_UINT32(1U, s_event_count);
    assert_event(0U, "put", "{\"path\":\"/\",\"data\":1}");
}

TEST_CASE("test_sse_parser_crlf_line_endings_dispatch_event", "[sse_parser]")
{
    feed("event: patch\r\ndata: {}\r\n\r\n");

    TEST_ASSERT_EQUAL_UINT32(1U, s_event_count);
    assert_event(0U, "patch", "{}");
}

TEST_CASE("test_sse_parser_event_split_at_every_byte_dispatches_once", "[sse_parser]")
{
    const char *stream = "event: put\r\ndata: {\"a\":1}\r\n\r\n";
    for (size_t i = 0U; i < strlen(stream); i++) {
        sse_parser_feed(s_parser, &stream[i], 1U);
    }

    TEST_ASSERT_EQUAL_UINT32(1U, s_event_count);
    assert_event(0U, "put", "{\"a\":1}");
}

TEST_CASE("test_sse_parser_two_events_in_one_chunk_dispatch_in_order", "[sse_parser]")
{
    feed("event: keep-alive\ndata: null\n\nevent: put\ndata: 2\n\n");

    TEST_ASSERT_EQUAL_UINT32(2U, s_event_count);
    assert_event(0U, "keep-alive", "null");
    assert_event(1U, "put", "2");
}

TEST_CASE("test_sse_parser_value_without_space_is_accepted", "[sse_parser]")
{
    feed("event:put\ndata:3\n\n");

    TEST_ASSERT_EQUAL_UINT32(1U, s_event_count);
    assert_event(0U, "put", "3");
}

TEST_CASE("test_sse_parser_event_without_data_reports_empty_data", "[sse_parser]")
{
    feed("event: keep-alive\n\n");

    TEST_ASSERT_EQUAL_UINT32(1U, s_event_count);
    assert_event(0U, "keep-alive", "");
}

TEST_CASE("test_sse_parser_block_without_event_is_ignored_and_data_not_leaked", "[sse_parser]")
{
    feed("data: orphan\n\n");
    TEST_ASSERT_EQUAL_UINT32(0U, s_event_count);

    feed("event: put\n\n");
    TEST_ASSERT_EQUAL_UINT32(1U, s_event_count);
    assert_event(0U, "put", "");
}

TEST_CASE("test_sse_parser_comments_and_unknown_fields_are_ignored", "[sse_parser]")
{
    feed(": comment\nid: 7\nevent: put\nretry: 10\ndata: v\n\n");

    TEST_ASSERT_EQUAL_UINT32(1U, s_event_count);
    assert_event(0U, "put", "v");
}

TEST_CASE("test_sse_parser_line_filling_buffer_is_kept", "[sse_parser]")
{
    const size_t value_len = SSE_PARSER_LINE_MAX_LEN - 1U - DATA_PREFIX_LEN;

    feed("event: put\n" DATA_PREFIX);
    feed(long_value(value_len));
    feed("\n\n");

    TEST_ASSERT_EQUAL_UINT32(1U, s_event_count);
    TEST_ASSERT_EQUAL_size_t(value_len, strlen(s_events[0].data));
}

TEST_CASE("test_sse_parser_overflowing_line_drops_event_and_recovers", "[sse_parser]")
{
    feed("event: put\n" DATA_PREFIX);
    feed(long_value(SSE_PARSER_LINE_MAX_LEN));
    feed("\n\n");
    TEST_ASSERT_EQUAL_UINT32(0U, s_event_count);

    feed("event: put\ndata: ok\n\n");
    TEST_ASSERT_EQUAL_UINT32(1U, s_event_count);
    assert_event(0U, "put", "ok");
}

TEST_CASE("test_sse_parser_reset_discards_partial_event", "[sse_parser]")
{
    feed("event: put\ndata: stale");
    sse_parser_reset(s_parser);
    feed("\n\n");

    TEST_ASSERT_EQUAL_UINT32(0U, s_event_count);
}

TEST_CASE("test_sse_parser_zero_bytes_is_a_no_op", "[sse_parser]")
{
    sse_parser_feed(s_parser, NULL, 0U);

    TEST_ASSERT_EQUAL_UINT32(0U, s_event_count);
}

TEST_CASE("test_sse_parser_context_pointer_reaches_handler", "[sse_parser]")
{
    static int32_t ctx_marker;
    sse_parser_deinit(s_parser);
    TEST_ASSERT_EQUAL_INT32(ESP_OK, sse_parser_init(capture_ctx, &ctx_marker, &s_parser));
    s_captured_ctx = NULL;

    feed("event: put\n\n");

    TEST_ASSERT_EQUAL_PTR(&ctx_marker, s_captured_ctx);
}

TEST_CASE("test_sse_parser_pool_exhausted_returns_no_mem", "[sse_parser]")
{
    sse_parser_handle_t second = NULL;

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_NO_MEM, sse_parser_init(record_event, NULL, &second));
    TEST_ASSERT_NULL(second);
}

TEST_CASE("test_sse_parser_deinit_then_init_returns_a_clean_parser", "[sse_parser]")
{
    feed("event: put\ndata: partial");
    sse_parser_deinit(s_parser);
    TEST_ASSERT_EQUAL_INT32(ESP_OK, sse_parser_init(record_event, NULL, &s_parser));

    feed("\n\n");

    TEST_ASSERT_EQUAL_UINT32(0U, s_event_count);
}
