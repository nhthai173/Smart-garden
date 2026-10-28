/**
 * @file    nht_webkit.h
 * @brief   HTTP server + WebSocket + over-the-air update over the LAN, for
 *          ESP-IDF.
 * @version 1.1.0
 * @license MIT
 *
 * Wraps `esp_http_server` together with what every IoT device ends up
 * rewriting: serving an embedded gzip page, answering JSON, reading query
 * parameters and request bodies, broadcasting over WebSocket, and a firmware
 * upload endpoint.
 *
 * @par About OTA
 * Firmware is uploaded straight over HTTP inside the local network and written
 * to the OTA partition block by block. There is no intermediate step - no
 * base64, no round trip through a cloud service, no whole firmware image held
 * in RAM. Downloading firmware through a cloud service and keeping it in memory
 * is a well-known source of hangs on chips with a small heap.
 *
 * After the write the new image is marked "pending verify". The application
 * must call `esp_ota_mark_app_valid_cancel_rollback()` once it runs correctly;
 * otherwise the bootloader rolls back to the old image on the next boot.
 *
 * @par Example
 * @code
 * static esp_err_t h_hello(httpd_req_t *r) { return nht_web_json(r, "{\"hi\":1}"); }
 *
 * nht_web_cfg_t cfg = { .enable_ota = true };
 * nht_web_start(&cfg);
 * nht_web_route("/api/hello", HTTP_GET, h_hello, NULL);
 * @endcode
 */

#ifndef NHT_WEBKIT_H
#define NHT_WEBKIT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Server configuration. Zero fields take the default shown. */
typedef struct {
    uint16_t port;              /**< default 80                              */
    size_t   stack_size;        /**< default 8192                            */
    uint16_t max_handlers;      /**< default 24                              */
    uint8_t  max_sockets;       /**< max simultaneous client connections; default 5.
                                     Keep it well below CONFIG_LWIP_MAX_SOCKETS so
                                     other sockets (cloud, DNS, SNTP) always find room */
    bool     enable_ws;         /**< serve a WebSocket endpoint              */
    const char *ws_path;        /**< default "/ws"                           */
    bool     enable_ota;        /**< serve the firmware upload endpoint      */
    const char *ota_path;       /**< default "/update"                       */
    bool     captive;           /**< redirect every unknown URL to "/"       */
    void (*on_ota_progress)(size_t done, size_t total, void *user); /**< OTA progress */
    void (*on_ota_done)(bool ok, void *user);                       /**< OTA finished */
    void  *user;                /**< passed to the OTA callbacks             */
} nht_web_cfg_t;

/**
 * Start the server.
 *
 * @param cfg configuration; NULL takes all defaults
 * @return ESP_OK or an ESP-IDF error
 */
esp_err_t      nht_web_start(const nht_web_cfg_t *cfg);

/** Stop the server. */
void           nht_web_stop(void);

/** @return the underlying `esp_http_server` handle, or NULL if not running. */
httpd_handle_t nht_web_handle(void);

/**
 * Register a route. Call after nht_web_start().
 *
 * @param uri     path to match
 * @param method  HTTP method
 * @param handler request handler
 * @param ctx     available to the handler as `r->user_ctx`
 */
esp_err_t nht_web_route(const char *uri, httpd_method_t method,
                        esp_err_t (*handler)(httpd_req_t *), void *ctx);

/** @name Reply helpers
 *  @{ */

/** Reply 200 with a JSON string. */
esp_err_t nht_web_json(httpd_req_t *r, const char *json);

/** Reply 200 with plain text. */
esp_err_t nht_web_text(httpd_req_t *r, const char *text);

/** Reply with an HTTP error code and a small JSON error body. */
esp_err_t nht_web_error(httpd_req_t *r, int code, const char *message);

/**
 * Serve a gzip-compressed byte array (an embedded page in flash).
 *
 * @param gz           compressed data
 * @param len          its length
 * @param content_type MIME type of the uncompressed content
 */
esp_err_t nht_web_gz(httpd_req_t *r, const uint8_t *gz, size_t len,
                     const char *content_type);
/** @} */

/** @name Request helpers
 *  @{ */

/**
 * Read one query-string parameter.
 *
 * @param key     parameter name
 * @param out     destination buffer
 * @param out_len size of @p out
 * @return the value length, or -1 if the parameter is absent
 */
int nht_web_query(httpd_req_t *r, const char *key, char *out, size_t out_len);

/**
 * Read the whole request body into @p buf (NUL-terminated).
 *
 * @return number of bytes read, or < 0 on error (including a body larger than
 *         the buffer)
 */
int nht_web_body(httpd_req_t *r, char *buf, size_t buf_len);

/**
 * Check that a JSON string nests no deeper than @p max_depth levels (`[` and `{`
 * outside strings).
 *
 * Call this before `cJSON_Parse()` on any untrusted input. cJSON recurses with
 * the depth (its default limit is 1000 levels), so a request body of a few
 * hundred `[` characters is enough to overflow the HTTP task's stack and reset
 * the device.
 *
 * @return true if the nesting is within the limit
 */
bool nht_json_depth_ok(const char *s, unsigned max_depth);
/** @} */

/** @name WebSocket
 *  @{ */

/**
 * Send a text frame to every open client. Safe when nobody is connected.
 */
esp_err_t nht_web_ws_broadcast(const char *text);

/** @return the number of connected WebSocket clients. */
int       nht_web_ws_client_count(void);

/**
 * Callback for text frames received from a client.
 *
 * @param r    the request (use it to identify the client)
 * @param text received text, NUL-terminated
 * @param user pointer given to nht_web_ws_on_message()
 */
typedef void (*nht_web_ws_cb_t)(httpd_req_t *r, const char *text, void *user);

/** Set the receive callback. Call before nht_web_start(). */
void nht_web_ws_on_message(nht_web_ws_cb_t cb, void *user);
/** @} */

#ifdef __cplusplus
}
#endif
#endif /* NHT_WEBKIT_H */
