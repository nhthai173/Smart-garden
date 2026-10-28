/**
 * @file    nht_sse.c
 * @brief   Incremental Server-Sent Events parser.
 *
 * See nht_sse.h for the public API and the design rationale.
 */

#include "nht_sse.h"
#include <string.h>

/*
 * The buffer holds several lines of the SAME event one after another:
 *
 *     [ event: put \0 ][ data: {...} \0 ][ line being collected... ]
 *       ^line_start moved back              ^line_start               ^len
 *
 * `line_start` is where the line being collected begins, `len` is where the
 * next byte goes. A line that need not be kept (event:, comments) rewinds `len`
 * to `line_start` so the space is reused; only the data: line is kept, because
 * the `data` pointer points into it and must stay valid until the blank line.
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

    s->buf[s->len] = '\0';   /* feed() always leaves one byte free for this */

    if (linelen == 0) { end_event(s); return; }

    if (!strncmp(line, "event:", 6)) {
        const char *v = line + 6;
        while (*v == ' ') v++;
        strncpy(s->event, v, NHT_SSE_EVENT_MAX - 1);
        s->event[NHT_SSE_EVENT_MAX - 1] = '\0';
        s->len = s->line_start;            /* this line need not be kept */

    } else if (!strncmp(line, "data:", 5)) {
        char *v = line + 5;
        while (*v == ' ') v++;
        s->data       = v;
        s->len       += 1;                 /* keep the '\0' byte just written */
        s->line_start = s->len;            /* the next line starts here        */

    } else {
        /* Comment line (":...") or an unused field: skip it. */
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

        /* +1 so there is always room for the '\0' when the line ends. */
        if (s->len + 1 < s->cap) {
            s->buf[s->len++] = c;
        } else {
            /* Do not truncate and pass it up: a cut JSON string can still parse into
               a valid-looking structure, far more dangerous than dropping the event. */
            s->overflow = true;
        }
    }
}
