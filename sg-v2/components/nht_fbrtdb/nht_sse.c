#include "nht_sse.h"
#include <string.h>

/*
 * Bộ đệm chứa nhiều dòng của CÙNG một sự kiện nối tiếp nhau:
 *
 *     [ event: put \0 ][ data: {...} \0 ][ dòng đang gom... ]
 *       ^line_start đã lùi                 ^line_start        ^len
 *
 * `line_start` là chỗ bắt đầu dòng đang gom, `len` là chỗ ghi tiếp. Dòng nào
 * không cần giữ lại (event:, chú thích) thì lùi `len` về `line_start` để tái
 * dùng chỗ; riêng dòng data: được giữ nguyên vì con trỏ `data` trỏ vào đó và
 * phải còn giá trị cho tới khi gặp dòng trống.
 */

void nht_sse_init(nht_sse_t *s, char *buf, size_t cap,
                  nht_sse_cb_t cb, void *user)
{
    memset(s, 0, sizeof(*s));
    s->buf  = buf;
    s->cap  = cap;
    s->cb   = cb;
    s->user = user;
}

void nht_sse_reset(nht_sse_t *s)
{
    s->len        = 0;
    s->line_start = 0;
    s->data       = NULL;
    s->overflow   = false;
    s->event[0]   = '\0';
}

static void end_event(nht_sse_t *s)
{
    if (s->overflow) {
        s->dropped++;
    } else if (s->event[0] && s->data && s->cb) {
        s->events++;
        s->cb(s->event, s->data, s->user);
    }
    s->event[0]   = '\0';
    s->data       = NULL;
    s->overflow   = false;
    s->len        = 0;
    s->line_start = 0;
}

static void end_line(nht_sse_t *s)
{
    char  *line    = s->buf + s->line_start;
    size_t linelen = s->len - s->line_start;

    s->buf[s->len] = '\0';   /* feed() luôn chừa sẵn một byte cho chỗ này */

    if (linelen == 0) { end_event(s); return; }

    if (!strncmp(line, "event:", 6)) {
        const char *v = line + 6;
        while (*v == ' ') v++;
        strncpy(s->event, v, NHT_SSE_EVENT_MAX - 1);
        s->event[NHT_SSE_EVENT_MAX - 1] = '\0';
        s->len = s->line_start;            /* dòng này không cần giữ */

    } else if (!strncmp(line, "data:", 5)) {
        char *v = line + 5;
        while (*v == ' ') v++;
        s->data       = v;
        s->len       += 1;                 /* giữ cả byte '\0' vừa ghi */
        s->line_start = s->len;            /* dòng sau bắt đầu từ đây  */

    } else {
        /* Dòng chú thích (":...") hoặc trường không dùng: bỏ. */
        s->len = s->line_start;
    }
}

void nht_sse_feed(nht_sse_t *s, const char *chunk, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char c = chunk[i];
        if (c == '\r') continue;

        if (c == '\n') {
            end_line(s);
            continue;
        }

        /* +1 để luôn còn chỗ ghi '\0' lúc kết thúc dòng. */
        if (s->len + 1 < s->cap) {
            s->buf[s->len++] = c;
        } else {
            /* Không cắt cụt rồi đẩy lên trên: JSON cụt vẫn parse ra được một
               cấu trúc trông hợp lệ, nguy hiểm hơn hẳn việc bỏ qua. */
            s->overflow = true;
        }
    }
}
