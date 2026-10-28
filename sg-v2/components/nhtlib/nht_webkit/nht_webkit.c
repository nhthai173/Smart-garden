/**
 * @file    nht_webkit.c
 * @brief   HTTP server helpers, WebSocket broadcast and OTA upload endpoint.
 *
 * See nht_webkit.h for the public API and the design rationale.
 */

#include "nht_webkit.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "nht_web";

#define OTA_CHUNK 1024

static httpd_handle_t   SRV;
static nht_web_cfg_t    CFG;
static nht_web_ws_cb_t  WS_CB;
static void            *WS_USER;

httpd_handle_t nht_web_handle(void) { return SRV; }

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

esp_err_t nht_web_json(httpd_req_t *r, const char *json)
{
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_sendstr(r, json);
}

esp_err_t nht_web_text(httpd_req_t *r, const char *text)
{
    httpd_resp_set_type(r, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(r, text);
}

esp_err_t nht_web_error(httpd_req_t *r, int code, const char *message)
{
    char buf[192];
    snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}",
             message ? message : "error");

    const char *status;
    switch (code) {
    case 400: status = "400 Bad Request";  break;
    case 404: status = "404 Not Found";    break;
    case 409: status = "409 Conflict";     break;
    case 503: status = "503 Service Unavailable"; break;
    default:  status = "500 Internal Server Error";
    }
    httpd_resp_set_status(r, status);
    return nht_web_json(r, buf);
}

esp_err_t nht_web_gz(httpd_req_t *r, const uint8_t *gz, size_t len,
                     const char *content_type)
{
    httpd_resp_set_type(r, content_type ? content_type : "text/html");
    httpd_resp_set_hdr(r, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(r, "Cache-Control", "max-age=600");
    return httpd_resp_send(r, (const char *)gz, len);
}

int nht_web_query(httpd_req_t *r, const char *key, char *out, size_t out_len)
{
    size_t qlen = httpd_req_get_url_query_len(r);
    if (qlen == 0) return -1;

    char *q = malloc(qlen + 1);
    if (!q) return -1;

    int ret = -1;
    if (httpd_req_get_url_query_str(r, q, qlen + 1) == ESP_OK) {
        if (httpd_query_key_value(q, key, out, out_len) == ESP_OK)
            ret = (int)strlen(out);
    }
    free(q);
    return ret;
}

int nht_web_body(httpd_req_t *r, char *buf, size_t buf_len)
{
    int total = r->content_len;
    if (total <= 0) { buf[0] = '\0'; return 0; }
    if ((size_t)total >= buf_len) return -1;   /* too large */

    int got = 0;
    while (got < total) {
        int n = httpd_req_recv(r, buf + got, total - got);
        if (n <= 0) {
            if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
            return -1;
        }
        got += n;
    }
    buf[got] = '\0';
    return got;
}

/* ------------------------------------------------------------------ */
/* WebSocket                                                          */
/* ------------------------------------------------------------------ */

void nht_web_ws_on_message(nht_web_ws_cb_t cb, void *user)
{
    WS_CB   = cb;
    WS_USER = user;
}

static esp_err_t ws_handler(httpd_req_t *r)
{
    if (r->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket mở");
        return ESP_OK;       /* handshake do server lo */
    }

    httpd_ws_frame_t f = { .type = HTTPD_WS_TYPE_TEXT };
    esp_err_t err = httpd_ws_recv_frame(r, &f, 0);
    if (err != ESP_OK || f.len == 0) return err;
    if (f.len > 1024) return ESP_FAIL;

    uint8_t *buf = calloc(1, f.len + 1);
    if (!buf) return ESP_ERR_NO_MEM;

    f.payload = buf;
    err = httpd_ws_recv_frame(r, &f, f.len);
    if (err == ESP_OK && f.type == HTTPD_WS_TYPE_TEXT && WS_CB)
        WS_CB(r, (const char *)buf, WS_USER);

    free(buf);
    return err;
}

int nht_web_ws_client_count(void)
{
    if (!SRV) return 0;
    size_t n = CONFIG_LWIP_MAX_LISTENING_TCP;
    int fds[CONFIG_LWIP_MAX_LISTENING_TCP];
    if (httpd_get_client_list(SRV, &n, fds) != ESP_OK) return 0;

    int c = 0;
    for (size_t i = 0; i < n; i++)
        if (httpd_ws_get_fd_info(SRV, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) c++;
    return c;
}

/* Asynchronous send: queue the work on the server instead of writing to the
   socket directly, so it can be called from any task without blocking it. */
typedef struct {
    httpd_handle_t hd;
    int fd;
    char *text;
} ws_send_t;

static void ws_send_cb(void *arg)
{
    ws_send_t *s = (ws_send_t *)arg;
    httpd_ws_frame_t f = {
        .type    = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)s->text,
        .len     = strlen(s->text),
    };
    httpd_ws_send_frame_async(s->hd, s->fd, &f);
    free(s->text);
    free(s);
}

esp_err_t nht_web_ws_broadcast(const char *text)
{
    if (!SRV || !text) return ESP_ERR_INVALID_STATE;

    size_t n = CONFIG_LWIP_MAX_LISTENING_TCP;
    int fds[CONFIG_LWIP_MAX_LISTENING_TCP];
    if (httpd_get_client_list(SRV, &n, fds) != ESP_OK) return ESP_FAIL;

    for (size_t i = 0; i < n; i++) {
        if (httpd_ws_get_fd_info(SRV, fds[i]) != HTTPD_WS_CLIENT_WEBSOCKET)
            continue;

        ws_send_t *s = calloc(1, sizeof(*s));
        if (!s) continue;
        s->hd   = SRV;
        s->fd   = fds[i];
        s->text = strdup(text);
        if (!s->text) { free(s); continue; }

        if (httpd_queue_work(SRV, ws_send_cb, s) != ESP_OK) {
            free(s->text);
            free(s);
        }
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* OTA qua LAN                                                        */
/* ------------------------------------------------------------------ */

static esp_err_t ota_handler(httpd_req_t *r)
{
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target) return nht_web_error(r, 500, "khong tim thay partition OTA");

    int total = r->content_len;
    if (total <= 0) return nht_web_error(r, 400, "thieu du lieu firmware");
    if ((size_t)total > target->size)
        return nht_web_error(r, 400, "firmware lon hon partition");

    ESP_LOGI(TAG, "OTA: nhận %d B vào '%s'", total, target->label);

    esp_ota_handle_t h;
    esp_err_t err = esp_ota_begin(target, total, &h);
    if (err != ESP_OK)
        return nht_web_error(r, 500, esp_err_to_name(err));

    char *buf = malloc(OTA_CHUNK);
    if (!buf) { esp_ota_abort(h); return nht_web_error(r, 500, "het bo nho"); }

    int got = 0;
    bool ok = true;
    bool header_checked = false;

    while (got < total) {
        int n = httpd_req_recv(r, buf, OTA_CHUNK);
        if (n <= 0) {
            if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ok = false;
            break;
        }

        /* Check the magic byte in the very first block: uploading a file that is
           not firmware would corrupt the partition, unnoticed until the next reset. */
        if (!header_checked) {
            if (n < 1 || (uint8_t)buf[0] != ESP_IMAGE_HEADER_MAGIC) {
                ESP_LOGE(TAG, "OTA: không phải ảnh firmware ESP");
                ok = false;
                break;
            }
            header_checked = true;
        }

        if (esp_ota_write(h, buf, n) != ESP_OK) { ok = false; break; }
        got += n;

        if (CFG.on_ota_progress)
            CFG.on_ota_progress(got, total, CFG.user);
    }
    free(buf);

    if (!ok || got != total) {
        esp_ota_abort(h);
        ESP_LOGE(TAG, "OTA hỏng ở %d/%d B", got, total);
        if (CFG.on_ota_done) CFG.on_ota_done(false, CFG.user);
        return nht_web_error(r, 400, "nap firmware that bai");
    }

    err = esp_ota_end(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA: ảnh không hợp lệ: %s", esp_err_to_name(err));
        if (CFG.on_ota_done) CFG.on_ota_done(false, CFG.user);
        return nht_web_error(r, 400, "anh firmware khong hop le");
    }

    err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        if (CFG.on_ota_done) CFG.on_ota_done(false, CFG.user);
        return nht_web_error(r, 500, esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "OTA xong, khởi động lại vào '%s'", target->label);
    nht_web_json(r, "{\"ok\":true,\"message\":\"Đã nạp xong, đang khởi động lại\"}");
    if (CFG.on_ota_done) CFG.on_ota_done(true, CFG.user);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */

static esp_err_t not_found(httpd_req_t *r, httpd_err_code_t err)
{
    (void)err;
    if (CFG.captive) {
        /* A phone probes for a captive portal by requesting any URL; a 302
           to the root is enough to make it open the setup page. */
        httpd_resp_set_status(r, "302 Found");
        httpd_resp_set_hdr(r, "Location", "/");
        return httpd_resp_send(r, NULL, 0);
    }
    httpd_resp_set_status(r, "404 Not Found");
    return nht_web_json(r, "{\"ok\":false,\"error\":\"khong tim thay\"}");
}

esp_err_t nht_web_route(const char *uri, httpd_method_t method,
                        esp_err_t (*handler)(httpd_req_t *), void *ctx)
{
    if (!SRV) return ESP_ERR_INVALID_STATE;
    httpd_uri_t u = {
        .uri      = uri,
        .method   = method,
        .handler  = handler,
        .user_ctx = ctx,
    };
    return httpd_register_uri_handler(SRV, &u);
}

esp_err_t nht_web_start(const nht_web_cfg_t *cfg)
{
    if (SRV) return ESP_OK;
    CFG = cfg ? *cfg : (nht_web_cfg_t){0};

    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.server_port      = CFG.port ? CFG.port : 80;
    c.ctrl_port        = c.server_port + 20000;
    c.stack_size       = CFG.stack_size ? CFG.stack_size : 8192;
    c.max_uri_handlers = CFG.max_handlers ? CFG.max_handlers : 24;
    c.lru_purge_enable = true;
    /* Cap client sockets. The chip has a fixed pool of lwIP sockets (see
       CONFIG_LWIP_MAX_SOCKETS) shared with the cloud client, DNS and SNTP; if
       HTTP clients could take them all, outgoing connections would fail with
       "No buffer space available" and a request flood would cut the device
       off from the cloud. */
    c.max_open_sockets = CFG.max_sockets ? CFG.max_sockets : 5;
    c.uri_match_fn     = httpd_uri_match_wildcard;
    /* OTA pushes several MB in one request: the default timeout is far too short. */
    c.recv_wait_timeout = 20;
    c.send_wait_timeout = 20;

    esp_err_t err = httpd_start(&SRV, &c);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "không khởi động được HTTP server: %s",
                 esp_err_to_name(err));
        return err;
    }

    if (CFG.enable_ws) {
        httpd_uri_t ws = {
            .uri          = CFG.ws_path ? CFG.ws_path : "/ws",
            .method       = HTTP_GET,
            .handler      = ws_handler,
            .is_websocket = true,
        };
        httpd_register_uri_handler(SRV, &ws);
    }

    if (CFG.enable_ota) {
        httpd_uri_t ota = {
            .uri     = CFG.ota_path ? CFG.ota_path : "/update",
            .method  = HTTP_POST,
            .handler = ota_handler,
        };
        httpd_register_uri_handler(SRV, &ota);
    }

    httpd_register_err_handler(SRV, HTTPD_404_NOT_FOUND, not_found);

    ESP_LOGI(TAG, "HTTP server chạy ở cổng %d", c.server_port);
    return ESP_OK;
}

void nht_web_stop(void)
{
    if (!SRV) return;
    httpd_stop(SRV);
    SRV = NULL;
}

bool nht_json_depth_ok(const char *s, unsigned max_depth)
{
    unsigned depth = 0;
    bool in_str = false, esc = false;
    for (; s && *s; s++) {
        char c = *s;
        if (in_str) {
            if (esc)            esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"')  in_str = false;
            continue;
        }
        if (c == '"') in_str = true;
        else if (c == '[' || c == '{') { if (++depth > max_depth) return false; }
        else if ((c == ']' || c == '}') && depth) depth--;
    }
    return true;
}
