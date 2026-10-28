/**
 * @file    nht_fbrtdb.h
 * @brief   Firebase Realtime Database client for ESP-IDF: email/password
 *          sign-in, REST CRUD, and an SSE stream.
 * @version 1.1.0
 * @license MIT
 *
 * There is no official Firebase library for ESP-IDF (the widely used ones are
 * written for Arduino), so this is built on `esp_http_client` and
 * `esp_crt_bundle`.
 *
 * @par Pitfalls handled here
 *  1. **307 redirects.** Old databases (`*.firebaseio.com`) do not serve a
 *     stream on the host in your URL: they answer 307 with a Location header
 *     pointing at another server (`s-xxx-nss-yyy.firebaseio.com`). Regional
 *     databases (`*.firebasedatabase.app`) serve it directly. The firmware must
 *     cope with both. Key point: in manual-connection mode - required to read a
 *     stream - `esp_http_client` does *not* follow redirects even if configured
 *     to. On an old database without this handling, the stream opens and then
 *     stays silent forever, with no error to trace.
 *  2. **Token expiry mid-stream.** The idToken lives for an hour and an open
 *     stream is cut when it expires. The token is refreshed 5 minutes early and
 *     the stream is re-opened with the new one.
 *  3. **Block reads.** Reading byte by byte is simple but wastes CPU; reads are
 *     done in blocks and split into lines afterwards.
 *  4. **Non-blocking writes.** The write calls queue work for a dedicated task.
 *     The caller (for example a valve-control task) never waits for a TLS
 *     handshake.
 *  5. **Self sign-in.** The worker task signs in on its own after
 *     nht_fb_start(); the caller only waits for the READY state.
 *  6. **Snapshot vs. change.** After every (re)connect the first event is a
 *     snapshot of the current data, announced by a synthetic "connected"
 *     event. Treat a snapshot as state to synchronise, not as a new command.
 *
 * @par Example
 * @code
 * static void on_event(const char *ev, const char *path, const char *data, void *u)
 * {
 *     if (!strcmp(ev, "connected")) { next_put_is_a_snapshot = true; return; }
 *     if (!strcmp(ev, "put")) apply(path, data);
 * }
 *
 * nht_fb_cfg_t cfg = { .api_key = KEY, .db_url = URL, .email = USER, .password = PASS };
 * nht_fb_start(&cfg);
 * // ... when nht_fb_state() == NHT_FB_READY:
 * nht_fb_stream_start("/uid/device/desired", on_event, NULL);
 * nht_fb_patch("/uid/device/reported", "{\"temp\":21.5}");   // returns immediately
 * @endcode
 */

#ifndef NHT_FBRTDB_H
#define NHT_FBRTDB_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Client state. */
typedef enum {
    NHT_FB_OFFLINE = 0,
    NHT_FB_AUTH,        /**< signing in / refreshing the token */
    NHT_FB_READY,
    NHT_FB_ERROR,
} nht_fb_state_t;

/** Client configuration. */
typedef struct {
    const char *api_key;     /**< Firebase web API key                       */
    const char *db_url;      /**< https://<project>.firebaseio.com (no trailing /) */
    const char *email;       /**< account used to sign in                    */
    const char *password;
    uint16_t    queue_depth; /**< pending-write queue length; 0 -> 8          */
    uint32_t    retry_ms;    /**< wait between retries; 0 -> 5000             */
} nht_fb_cfg_t;

/**
 * Stream event callback.
 *
 * @param event  "put" | "patch" | "keep-alive" | "cancel" | "auth_revoked"
 *               | "connected". "connected" is raised by this library every time
 *               the stream opens, including re-opens after a drop or a token
 *               change; it is immediately followed by a "put" on "/" carrying a
 *               SNAPSHOT of the current data - not a new change.
 * @param path   path relative to the streamed path, for example "/valve"
 * @param data   raw JSON of the "data" field (may be "null")
 * @param user   pointer given to nht_fb_stream_start()
 */
typedef void (*nht_fb_stream_cb_t)(const char *event, const char *path,
                                   const char *data, void *user);

/**
 * State-change callback.
 *
 * @param st   the new state
 * @param user pointer given to nht_fb_on_state()
 */
typedef void (*nht_fb_state_cb_t)(nht_fb_state_t st, void *user);

/**
 * Start the client (creates the worker task, which signs in by itself).
 *
 * @return ESP_OK, ESP_ERR_INVALID_ARG or ESP_ERR_NO_MEM
 */
esp_err_t      nht_fb_start(const nht_fb_cfg_t *cfg);

/** Stop the client and its stream. */
void           nht_fb_stop(void);

/** @return the current state. */
nht_fb_state_t nht_fb_state(void);

/** Register the state-change callback (one only). */
void           nht_fb_on_state(nht_fb_state_cb_t cb, void *user);

/** @return the account uid after sign-in; an empty string before. */
const char *nht_fb_uid(void);

/** @name Asynchronous writes - return at once, a background task does the work
 *  If the queue is full the oldest entry is dropped: these calls carry status
 *  reports, where a newer message always supersedes an older one.
 *  @{ */
esp_err_t nht_fb_put(const char *path, const char *json);
esp_err_t nht_fb_patch(const char *path, const char *json);
esp_err_t nht_fb_delete(const char *path);
/** @} */

/** @name Synchronous calls - block until done. Do NOT call from a control task.
 *  @{ */

/**
 * Read a node.
 *
 * @param path    database path
 * @param out     buffer for the JSON response
 * @param out_len size of @p out
 */
esp_err_t nht_fb_get_sync(const char *path, char *out, size_t out_len);

/** Write a node and wait for the result. */
esp_err_t nht_fb_put_sync(const char *path, const char *json);
/** @} */

/**
 * Open an SSE stream on @p path. Only one stream at a time is supported, which
 * is enough for the desired/reported model and saves a TLS connection on a
 * small chip.
 *
 * @param path database path to listen on
 * @param cb   called for every event, from the stream task
 * @param user passed to @p cb
 */
esp_err_t nht_fb_stream_start(const char *path, nht_fb_stream_cb_t cb,
                              void *user);

/** Close the stream. */
void      nht_fb_stream_stop(void);

/** @return true while the stream connection is open. */
bool      nht_fb_stream_connected(void);

#ifdef __cplusplus
}
#endif
#endif /* NHT_FBRTDB_H */
