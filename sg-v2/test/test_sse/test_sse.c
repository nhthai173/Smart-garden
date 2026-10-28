/**
 * Hồi quy cho nht_sse.
 * Điểm chính: dữ liệu từ socket đến theo khối tuỳ ý. Test nạp cùng một luồng
 * byte theo nhiều cách cắt khác nhau và đòi kết quả phải giống hệt nhau.
 */
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include "nht_sse.h"

#define BUF 512

static nht_sse_t S;
static char      BUFFER[BUF];
static char      last_event[64];
static char      last_data[BUF];
static int       calls;

static void on_ev(const char *event, const char *data, void *user)
{
    (void)user;
    snprintf(last_event, sizeof(last_event), "%s", event);
    snprintf(last_data, sizeof(last_data), "%s", data);
    calls++;
}

void setUp(void)
{
    memset(BUFFER, 0, sizeof(BUFFER));
    last_event[0] = last_data[0] = '\0';
    calls = 0;
    nht_sse_init(&S, BUFFER, BUF, on_ev, NULL);
}
void tearDown(void) { }

static void feed_str(const char *s) { nht_sse_feed(&S, s, strlen(s)); }

/** Nạp từng khối `sz` byte một - mô phỏng socket trả về từng mẩu nhỏ. */
static void feed_chunked(const char *s, size_t sz)
{
    size_t n = strlen(s);
    for (size_t i = 0; i < n; i += sz)
        nht_sse_feed(&S, s + i, (i + sz > n) ? (n - i) : sz);
}

/* --- cơ bản --- */

static void test_single_event(void)
{
    feed_str("event: put\ndata: {\"path\":\"/valve\",\"data\":null}\n\n");
    TEST_ASSERT_EQUAL_INT(1, calls);
    TEST_ASSERT_EQUAL_STRING("put", last_event);
    TEST_ASSERT_EQUAL_STRING("{\"path\":\"/valve\",\"data\":null}", last_data);
}

static void test_two_events(void)
{
    feed_str("event: put\ndata: {\"path\":\"/a\"}\n\n"
             "event: patch\ndata: {\"path\":\"/b\"}\n\n");
    TEST_ASSERT_EQUAL_INT(2, calls);
    TEST_ASSERT_EQUAL_STRING("patch", last_event);
    TEST_ASSERT_EQUAL_STRING("{\"path\":\"/b\"}", last_data);
}

static void test_crlf(void)
{
    feed_str("event: put\r\ndata: {\"x\":1}\r\n\r\n");
    TEST_ASSERT_EQUAL_INT(1, calls);
    TEST_ASSERT_EQUAL_STRING("{\"x\":1}", last_data);
}

static void test_no_space_after_colon(void)
{
    feed_str("event:put\ndata:{\"x\":1}\n\n");
    TEST_ASSERT_EQUAL_INT(1, calls);
    TEST_ASSERT_EQUAL_STRING("put", last_event);
    TEST_ASSERT_EQUAL_STRING("{\"x\":1}", last_data);
}

/* --- cắt khối tuỳ ý: kết quả phải bất biến --- */

static void test_chunk_size_does_not_matter(void)
{
    const char *stream =
        "event: put\ndata: {\"path\":\"/valve\",\"data\":{\"cmd\":\"open\"}}\n\n";
    const char *want = "{\"path\":\"/valve\",\"data\":{\"cmd\":\"open\"}}";

    for (size_t sz = 1; sz <= 40; sz++) {
        setUp();
        feed_chunked(stream, sz);
        char msg[64];
        snprintf(msg, sizeof(msg), "khối %zu byte", sz);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, calls, msg);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(want, last_data, msg);
    }
}

static void test_split_mid_line(void)
{
    feed_str("event: pu");
    feed_str("t\ndat");
    feed_str("a: {\"x\":");
    feed_str("1}\n");
    TEST_ASSERT_EQUAL_INT(0, calls);      /* chưa gặp dòng trống */
    feed_str("\n");
    TEST_ASSERT_EQUAL_INT(1, calls);
    TEST_ASSERT_EQUAL_STRING("{\"x\":1}", last_data);
}

/* --- những gì Firebase thực sự gửi --- */

static void test_keep_alive(void)
{
    feed_str("event: keep-alive\ndata: null\n\n");
    TEST_ASSERT_EQUAL_INT(1, calls);
    TEST_ASSERT_EQUAL_STRING("keep-alive", last_event);
    TEST_ASSERT_EQUAL_STRING("null", last_data);
}

static void test_auth_revoked(void)
{
    feed_str("event: auth_revoked\ndata: credential is no longer valid\n\n");
    TEST_ASSERT_EQUAL_INT(1, calls);
    TEST_ASSERT_EQUAL_STRING("auth_revoked", last_event);
}

static void test_keepalive_between_puts(void)
{
    feed_str("event: put\ndata: {\"path\":\"/\",\"data\":{}}\n\n");
    feed_str("event: keep-alive\ndata: null\n\n");
    feed_str("event: keep-alive\ndata: null\n\n");
    feed_str("event: put\ndata: {\"path\":\"/valve\",\"data\":1}\n\n");
    TEST_ASSERT_EQUAL_INT(4, calls);
    TEST_ASSERT_EQUAL_STRING("put", last_event);
    TEST_ASSERT_EQUAL_STRING("{\"path\":\"/valve\",\"data\":1}", last_data);
}

/* --- trường hợp hỏng --- */

static void test_oversized_event_is_dropped_not_truncated(void)
{
    /* JSON cụt vẫn parse ra được cấu trúc trông hợp lệ -> phải bỏ cả sự kiện. */
    char big[BUF * 2];
    int n = snprintf(big, sizeof(big), "event: put\ndata: {\"x\":\"");
    memset(big + n, 'A', BUF + 50);
    snprintf(big + n + BUF + 50, sizeof(big) - n - BUF - 50, "\"}\n\n");

    feed_str(big);
    TEST_ASSERT_EQUAL_INT(0, calls);
    TEST_ASSERT_EQUAL_UINT32(1, S.dropped);

    /* Và sự kiện tiếp theo vẫn phải nhận được bình thường. */
    feed_str("event: put\ndata: {\"ok\":1}\n\n");
    TEST_ASSERT_EQUAL_INT(1, calls);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":1}", last_data);
}

static void test_data_without_event_is_ignored(void)
{
    feed_str("data: {\"x\":1}\n\n");
    TEST_ASSERT_EQUAL_INT(0, calls);
}

static void test_comment_lines_ignored(void)
{
    feed_str(": đây là chú thích giữ kết nối\n");
    feed_str("event: put\ndata: {\"x\":1}\n\n");
    TEST_ASSERT_EQUAL_INT(1, calls);
    TEST_ASSERT_EQUAL_STRING("{\"x\":1}", last_data);
}

static void test_reset_clears_partial(void)
{
    feed_str("event: put\ndata: {\"x\":");
    nht_sse_reset(&S);
    feed_str("event: put\ndata: {\"y\":2}\n\n");
    TEST_ASSERT_EQUAL_INT(1, calls);
    TEST_ASSERT_EQUAL_STRING("{\"y\":2}", last_data);
}

static void test_empty_data_object(void)
{
    feed_str("event: put\ndata: {\"path\":\"/\",\"data\":null}\n\n");
    TEST_ASSERT_EQUAL_INT(1, calls);
    TEST_ASSERT_EQUAL_STRING("{\"path\":\"/\",\"data\":null}", last_data);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_single_event);
    RUN_TEST(test_two_events);
    RUN_TEST(test_crlf);
    RUN_TEST(test_no_space_after_colon);
    RUN_TEST(test_chunk_size_does_not_matter);
    RUN_TEST(test_split_mid_line);
    RUN_TEST(test_keep_alive);
    RUN_TEST(test_auth_revoked);
    RUN_TEST(test_keepalive_between_puts);
    RUN_TEST(test_oversized_event_is_dropped_not_truncated);
    RUN_TEST(test_data_without_event_is_ignored);
    RUN_TEST(test_comment_lines_ignored);
    RUN_TEST(test_reset_clears_partial);
    RUN_TEST(test_empty_data_object);
    return UNITY_END();
}
