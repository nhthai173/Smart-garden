/**
 * @file    nht_sse.h
 * @brief   Incremental Server-Sent Events parser.
 * @version 1.0.0
 * @license MIT
 *
 * Pure logic with no ESP-IDF dependency - it compiles and unit-tests on a PC.
 *
 * Data from a socket arrives in arbitrary chunks, cut in the middle of a line,
 * in the middle of an event, even between two bytes of a UTF-8 character. The
 * parser keeps its state between feeds, so the caller simply pours in whatever
 * it read.
 *
 * Format of one event:
 * @code
 * event: put
 * data: {"path":"/valve","data":{...}}
 * <blank line>
 * @endcode
 *
 * A `data` line longer than the buffer is flagged as overflowed and the *whole
 * event is dropped*, instead of passing a truncated JSON string upward. A cut
 * JSON string is worse than nothing because it can still parse into a
 * valid-looking structure.
 *
 * With several `data` lines in one event the last one wins (Firebase does not
 * use this; the SSE standard joins them with newlines).
 *
 * @par Example
 * @code
 * static char line[4096];
 * nht_sse_t sse;
 * nht_sse_init(&sse, line, sizeof line, on_event, NULL);
 * // for each chunk read from the socket:
 * nht_sse_feed(&sse, chunk, n);
 * @endcode
 */

#ifndef NHT_SSE_H
#define NHT_SSE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest event name kept (longer names are truncated). */
#define NHT_SSE_EVENT_MAX 32

/**
 * Called when a complete event has been collected (its terminating blank line
 * has been seen).
 *
 * @param event event name, for example "put"
 * @param data  the data field
 * @param user  pointer given to nht_sse_init()
 */
typedef void (*nht_sse_cb_t)(const char *event, const char *data, void *user);

/** Parser state. Treat as read-only. */
typedef struct {
    char   *buf;        /**< line buffer supplied by the caller              */
    size_t  cap;
    size_t  len;        /**< where the next byte is written in buf           */
    size_t  line_start; /**< start of the line being collected               */
    char    event[NHT_SSE_EVENT_MAX];
    char   *data;       /**< points into buf, or NULL if no data yet         */
    bool    overflow;
    nht_sse_cb_t cb;
    void   *user;
    uint32_t events;    /**< events delivered so far - handy in tests        */
    uint32_t dropped;   /**< events dropped for being too long               */
} nht_sse_t;

/**
 * Initialise the parser.
 *
 * @param s    parser
 * @param buf  line buffer; must outlive @p s. Use >= 4096 bytes for Firebase
 * @param cap  size of @p buf
 * @param cb   event callback
 * @param user passed to @p cb
 */
void nht_sse_init(nht_sse_t *s, char *buf, size_t cap,
                  nht_sse_cb_t cb, void *user);

/** Discard any half-collected event (call when the connection is re-opened). */
void nht_sse_reset(nht_sse_t *s);

/**
 * Feed a chunk of bytes just read from the socket.
 *
 * @param chunk data
 * @param n     number of bytes
 */
void nht_sse_feed(nht_sse_t *s, const char *chunk, size_t n);

#ifdef __cplusplus
}
#endif
#endif /* NHT_SSE_H */
