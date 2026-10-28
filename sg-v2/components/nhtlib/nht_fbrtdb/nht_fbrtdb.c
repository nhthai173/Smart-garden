/**
 * @file    nht_fbrtdb.c
 * @brief   Firebase Realtime Database client: sign-in, REST worker and SSE stream tasks.
 *
 * See nht_fbrtdb.h for the public API and the design rationale.
 */

#include "nht_fbrtdb.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "nht_sse.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define STREAM_READ_TIMEOUT_MS 3000   /* = worst-case delay of one event */

static const char *TAG = "nht_fb";

#define TOKEN_MAX      1400   /* a Firebase JWT is about 900-1100 bytes */
#define REFRESH_MAX     600
#define UID_MAX          40
#define URL_MAX         512
#define AUTH_RESP_MAX  3072
#define STREAM_CHUNK    512
#define STREAM_LINE    4096
#define REST_RESP_MAX  4096

#define TOKEN_EARLY_S   300   /* refresh 5 minutes before expiry */

typedef enum { OP_PUT, OP_PATCH, OP_DELETE } op_t;

typedef struct {
    op_t  op;
    char *path;
    char *json;
} job_t;

static struct {
    char api_key[80];
    char db_url[160];
    char email[80];
    char password[80];
    uint32_t retry_ms;
} CFG;

static char  ID_TOKEN[TOKEN_MAX];
static char  REFRESH_TOKEN[REFRESH_MAX];
static char  UID[UID_MAX];
static int64_t TOKEN_EXPIRES_US;

static nht_fb_state_t     STATE = NHT_FB_OFFLINE;
static nht_fb_state_cb_t  STATE_CB;
static void              *STATE_USER;

static QueueHandle_t      JOBS;
static SemaphoreHandle_t  TOKEN_LOCK;
static TaskHandle_t       WORKER, STREAMER;
static volatile bool      RUNNING;

static char               STREAM_PATH[160];
static nht_fb_stream_cb_t STREAM_CB;
static void              *STREAM_USER;
static volatile bool      STREAM_ON, STREAM_CONNECTED;

/* ------------------------------------------------------------------ */

static void set_state(nht_fb_state_t s)
{
    if (STATE == s) return;
    STATE = s;
    if (STATE_CB) STATE_CB(s, STATE_USER);
}

const char *nht_fb_uid(void) { return UID; }
nht_fb_state_t nht_fb_state(void) { return STATE; }
bool nht_fb_stream_connected(void) { return STREAM_CONNECTED; }

void nht_fb_on_state(nht_fb_state_cb_t cb, void *user)
{
    STATE_CB   = cb;
    STATE_USER = user;
}

static void token_get(char *out, size_t len)
{
    xSemaphoreTake(TOKEN_LOCK, portMAX_DELAY);
    strncpy(out, ID_TOKEN, len - 1);
    out[len - 1] = '\0';
    xSemaphoreGive(TOKEN_LOCK);
}

static bool token_valid(void)
{
    return ID_TOKEN[0] && esp_timer_get_time() < TOKEN_EXPIRES_US;
}

/* ------------------------------------------------------------------ */
/* HTTP: one synchronous call shared by sign-in and REST                */
/* ------------------------------------------------------------------ */

/* One persistent client with keep-alive, shared by sign-in and REST.
 *
 * A TLS handshake costs seconds of CPU and ~40 KB of heap on this chip. Opening
 * a fresh connection for every status report made a busy device spend nearly
 * all its time in crypto and run out of memory. Reusing one connection makes a
 * report cost a single small round trip. The connection is dropped after
 * KA_IDLE_US without use, and rebuilt on any error. */
#define KA_IDLE_US (30LL * 1000000)

typedef struct { char *buf; size_t cap; size_t len; } rx_t;

static SemaphoreHandle_t         HTTP_LOCK;
static esp_http_client_handle_t  KA;
static int64_t                   KA_LAST_US;
static rx_t                     *RX;      /* destination of the current response */

static esp_err_t on_http_event(esp_http_client_event_t *e)
{
    if (e->event_id == HTTP_EVENT_ON_DATA && RX && RX->buf && RX->cap) {
        size_t room = RX->cap - 1 - RX->len;
        size_t n = (size_t)e->data_len < room ? (size_t)e->data_len : room;
        memcpy(RX->buf + RX->len, e->data, n);
        RX->len += n;
        RX->buf[RX->len] = '\0';
    }
    return ESP_OK;
}

static void ka_drop(void)
{
    if (KA) { esp_http_client_cleanup(KA); KA = NULL; }
}

/** Close the shared connection if it has been idle for a while. */
static void ka_idle_close(void)
{
    if (!HTTP_LOCK || xSemaphoreTake(HTTP_LOCK, 0) != pdTRUE) return;
    if (KA && esp_timer_get_time() - KA_LAST_US > KA_IDLE_US) ka_drop();
    xSemaphoreGive(HTTP_LOCK);
}

/**
 * @param resp  buffer for the response body; NULL if not needed
 * @return HTTP status code, or a negative value for a lower-layer error
 */
static int http_do(const char *url, esp_http_client_method_t method,
                   const char *ctype, const char *body,
                   char *resp, size_t resp_len)
{
    if (!HTTP_LOCK) return -1;
    xSemaphoreTake(HTTP_LOCK, portMAX_DELAY);

    int status = -1;
    for (int attempt = 0; attempt < 2; attempt++) {
        bool reused = (KA != NULL);

        if (!KA) {
            esp_http_client_config_t c = {
                .url               = url,
                .timeout_ms        = 15000,
                .crt_bundle_attach = esp_crt_bundle_attach,
                .buffer_size       = 3072,   /* Google's headers are longer than 1 KB */
                .buffer_size_tx    = 3072,   /* the URL carries a ~900-byte idToken    */
                .event_handler     = on_http_event,
                .keep_alive_enable = true,
            };
            KA = esp_http_client_init(&c);
            if (!KA) { status = -1; break; }
        } else {
            esp_http_client_set_url(KA, url);   /* a new host reconnects by itself */
        }

        esp_http_client_set_method(KA, method);
        esp_http_client_set_header(KA, "Content-Type", ctype ? ctype : "application/json");
        esp_http_client_set_post_field(KA, body, body ? (int)strlen(body) : 0);

        rx_t rx = { resp, resp_len, 0 };
        if (resp && resp_len) resp[0] = '\0';
        RX = &rx;
        esp_err_t err = esp_http_client_perform(KA);
        RX = NULL;

        if (err == ESP_OK) {
            status = esp_http_client_get_status_code(KA);
            KA_LAST_US = esp_timer_get_time();
            break;
        }

        /* A kept-alive connection the server has since closed fails on first
           use: rebuild the client and try once more before giving up. */
        ESP_LOGW(TAG, "request failed: %s%s", esp_err_to_name(err),
                 reused ? " (retrying on a fresh connection)" : "");
        ka_drop();
        status = (err == ESP_ERR_HTTP_CONNECT) ? -2 : -4;
        if (!reused) break;
    }

    xSemaphoreGive(HTTP_LOCK);
    return status;
}

/* ------------------------------------------------------------------ */
/* Sign-in and token refresh                                           */
/* ------------------------------------------------------------------ */

/* cJSON recurses with the nesting depth: reject JSON nested too deeply before
   parsing, or a few hundred "[" characters can overflow the stream task's stack. */
static bool depth_ok(const char *s, unsigned max_depth)
{
    unsigned d = 0;
    bool in_str = false, esc = false;
    for (; s && *s; s++) {
        if (in_str) {
            if (esc) esc = false;
            else if (*s == '\\') esc = true;
            else if (*s == '"') in_str = false;
        } else if (*s == '"') in_str = true;
        else if (*s == '[' || *s == '{') { if (++d > max_depth) return false; }
        else if ((*s == ']' || *s == '}') && d) d--;
    }
    return true;
}

static bool parse_auth(const char *json, bool refresh_form)
{
    cJSON *j = cJSON_Parse(json);
    if (!j) return false;

    /* The two endpoints use different field names: signInWithPassword is
       camelCase, securetoken uses underscores. */
    const char *k_id  = refresh_form ? "id_token"      : "idToken";
    const char *k_rt  = refresh_form ? "refresh_token" : "refreshToken";
    const char *k_exp = refresh_form ? "expires_in"    : "expiresIn";
    const char *k_uid = refresh_form ? "user_id"       : "localId";

    cJSON *id = cJSON_GetObjectItem(j, k_id);
    cJSON *rt = cJSON_GetObjectItem(j, k_rt);
    cJSON *ex = cJSON_GetObjectItem(j, k_exp);
    cJSON *ui = cJSON_GetObjectItem(j, k_uid);

    if (!cJSON_IsString(id) || strlen(id->valuestring) >= TOKEN_MAX) {
        cJSON *e = cJSON_GetObjectItem(j, "error");
        if (e) {
            cJSON *m = cJSON_GetObjectItem(e, "message");
            ESP_LOGE(TAG, "Firebase từ chối: %s",
                     cJSON_IsString(m) ? m->valuestring : "?");
        }
        cJSON_Delete(j);
        return false;
    }

    xSemaphoreTake(TOKEN_LOCK, portMAX_DELAY);
    strncpy(ID_TOKEN, id->valuestring, TOKEN_MAX - 1);
    if (cJSON_IsString(rt) && strlen(rt->valuestring) < REFRESH_MAX)
        strncpy(REFRESH_TOKEN, rt->valuestring, REFRESH_MAX - 1);
    if (cJSON_IsString(ui) && strlen(ui->valuestring) < UID_MAX)
        strncpy(UID, ui->valuestring, UID_MAX - 1);

    int secs = 3600;
    if (cJSON_IsString(ex))      secs = atoi(ex->valuestring);
    else if (cJSON_IsNumber(ex)) secs = ex->valueint;
    if (secs < 60) secs = 60;

    TOKEN_EXPIRES_US = esp_timer_get_time() +
                       (int64_t)(secs - TOKEN_EARLY_S) * 1000000LL;
    xSemaphoreGive(TOKEN_LOCK);

    cJSON_Delete(j);
    return true;
}

static bool sign_in(void)
{
    set_state(NHT_FB_AUTH);

    char url[URL_MAX];
    snprintf(url, sizeof(url),
             "https://identitytoolkit.googleapis.com"
             "/v1/accounts:signInWithPassword?key=%s", CFG.api_key);

    char *body = malloc(320);
    char *resp = malloc(AUTH_RESP_MAX);
    if (!body || !resp) { free(body); free(resp); return false; }

    snprintf(body, 320,
             "{\"email\":\"%s\",\"password\":\"%s\",\"returnSecureToken\":true}",
             CFG.email, CFG.password);

    int st = http_do(url, HTTP_METHOD_POST, "application/json", body,
                     resp, AUTH_RESP_MAX);
    bool ok = (st == 200) && parse_auth(resp, false);

    if (ok) ESP_LOGI(TAG, "đăng nhập Firebase thành công, uid=%s", UID);
    else    ESP_LOGE(TAG, "đăng nhập Firebase thất bại (HTTP %d)", st);

    free(body);
    free(resp);
    return ok;
}

static bool refresh_token(void)
{
    if (!REFRESH_TOKEN[0]) return sign_in();
    set_state(NHT_FB_AUTH);

    char url[URL_MAX];
    snprintf(url, sizeof(url),
             "https://securetoken.googleapis.com/v1/token?key=%s", CFG.api_key);

    size_t blen = REFRESH_MAX + 64;
    char *body = malloc(blen);
    char *resp = malloc(AUTH_RESP_MAX);
    if (!body || !resp) { free(body); free(resp); return false; }

    snprintf(body, blen, "grant_type=refresh_token&refresh_token=%s",
             REFRESH_TOKEN);

    int st = http_do(url, HTTP_METHOD_POST,
                     "application/x-www-form-urlencoded", body,
                     resp, AUTH_RESP_MAX);
    bool ok = (st == 200) && parse_auth(resp, true);

    if (ok) ESP_LOGI(TAG, "đã làm mới token");
    else {
        ESP_LOGW(TAG, "làm mới token hỏng (HTTP %d), đăng nhập lại", st);
        REFRESH_TOKEN[0] = '\0';
        ok = sign_in();
    }

    free(body);
    free(resp);
    return ok;
}

static bool ensure_token(void)
{
    if (token_valid()) return true;
    return ID_TOKEN[0] ? refresh_token() : sign_in();
}

/* ------------------------------------------------------------------ */
/* REST                                                                */
/* ------------------------------------------------------------------ */

static void build_url(char *out, size_t len, const char *path)
{
    char tok[TOKEN_MAX];
    token_get(tok, sizeof(tok));
    snprintf(out, len, "%s%s.json?auth=%s", CFG.db_url, path, tok);
}

static esp_err_t rest_do(op_t op, const char *path, const char *json,
                         char *resp, size_t resp_len)
{
    if (!ensure_token()) return ESP_ERR_INVALID_STATE;

    size_t ulen = URL_MAX + TOKEN_MAX;
    char *url = malloc(ulen);
    if (!url) return ESP_ERR_NO_MEM;
    build_url(url, ulen, path);

    esp_http_client_method_t m =
        (op == OP_PUT)    ? HTTP_METHOD_PUT   :
        (op == OP_PATCH)  ? HTTP_METHOD_PATCH :
        (op == OP_DELETE) ? HTTP_METHOD_DELETE : HTTP_METHOD_GET;

    int st = http_do(url, m, "application/json", json, resp, resp_len);
    free(url);

    if (st == 401 || st == 403) {
        /* The token was just invalidated: refresh and retry exactly once. */
        ESP_LOGW(TAG, "%s %s -> %d, làm mới token và thử lại",
                 json ? "ghi" : "đọc", path, st);
        TOKEN_EXPIRES_US = 0;
        if (!ensure_token()) return ESP_ERR_INVALID_STATE;

        url = malloc(ulen);
        if (!url) return ESP_ERR_NO_MEM;
        build_url(url, ulen, path);
        st = http_do(url, m, "application/json", json, resp, resp_len);
        free(url);
    }

    if (st >= 200 && st < 300) {
        set_state(NHT_FB_READY);
        return ESP_OK;
    }
    ESP_LOGW(TAG, "REST %s hỏng: HTTP %d", path, st);
    return ESP_FAIL;
}

esp_err_t nht_fb_get_sync(const char *path, char *out, size_t out_len)
{
    return rest_do((op_t)-1, path, NULL, out, out_len);
}

esp_err_t nht_fb_put_sync(const char *path, const char *json)
{
    return rest_do(OP_PUT, path, json, NULL, 0);
}

/* --- asynchronous write queue --- */

static esp_err_t enqueue(op_t op, const char *path, const char *json)
{
    if (!JOBS) return ESP_ERR_INVALID_STATE;

    job_t j = { .op = op, .path = strdup(path),
                .json = json ? strdup(json) : NULL };
    if (!j.path || (json && !j.json)) {
        free(j.path); free(j.json);
        return ESP_ERR_NO_MEM;
    }

    if (xQueueSend(JOBS, &j, 0) == pdTRUE) return ESP_OK;

    /* Queue full: drop the oldest entry. These messages are status reports,
       a newer one always supersedes an older one - keeping the old one is pointless. */
    job_t old;
    if (xQueueReceive(JOBS, &old, 0) == pdTRUE) {
        free(old.path);
        free(old.json);
    }
    if (xQueueSend(JOBS, &j, 0) == pdTRUE) return ESP_OK;

    free(j.path); free(j.json);
    return ESP_FAIL;
}

esp_err_t nht_fb_put(const char *path, const char *json)
{
    return enqueue(OP_PUT, path, json);
}
esp_err_t nht_fb_patch(const char *path, const char *json)
{
    return enqueue(OP_PATCH, path, json);
}
esp_err_t nht_fb_delete(const char *path)
{
    return enqueue(OP_DELETE, path, NULL);
}

static void worker_task(void *arg)
{
    (void)arg;
    int64_t next_login_us = 0;
    while (RUNNING) {
        job_t j;
        if (xQueueReceive(JOBS, &j, pdMS_TO_TICKS(1000)) != pdTRUE) {
            if (!ID_TOKEN[0]) {
                /* No token yet: sign in BY OURSELVES, without waiting to be told. Sign-in
                   used to happen only when somebody queued work, while the caller waited
                   for the sign-in before queueing anything - so both waited forever.
                   On failure, retry after retry_ms. */
                if (esp_timer_get_time() >= next_login_us) {
                    if (sign_in()) set_state(NHT_FB_READY);
                    else {
                        set_state(NHT_FB_ERROR);
                        next_login_us = esp_timer_get_time() + (int64_t)CFG.retry_ms * 1000;
                    }
                }
            } else if (!token_valid()) {
                /* Have a token that is about to expire: refresh it first. */
                refresh_token();
            }
            ka_idle_close();   /* free the TLS session while nothing is happening */
            continue;
        }

        esp_err_t err = rest_do(j.op, j.path, j.json, NULL, 0);
        if (err != ESP_OK) {
            set_state(NHT_FB_ERROR);
            vTaskDelay(pdMS_TO_TICKS(CFG.retry_ms));
        }
        free(j.path);
        free(j.json);
    }
    WORKER = NULL;
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ */
/* Stream SSE                                                          */
/* ------------------------------------------------------------------ */

/** Handle one complete SSE event (called by nht_sse). */
static void dispatch(const char *event, const char *data, void *user)
{
    (void)user;
    if (!event || !event[0]) return;

    if (!strcmp(event, "keep-alive")) return;

    if (!strcmp(event, "auth_revoked")) {
        ESP_LOGW(TAG, "stream: token bị thu hồi, sẽ đăng nhập lại");
        TOKEN_EXPIRES_US = 0;
        STREAM_CONNECTED = false;
        if (STREAM_CB) STREAM_CB(event, "/", "null", STREAM_USER);
        return;
    }

    /* put/patch: data = {"path":"/x","data":<anything>} */
    cJSON *j = depth_ok(data, 16) ? cJSON_Parse(data) : NULL;
    if (!j) return;

    cJSON *jp = cJSON_GetObjectItem(j, "path");
    cJSON *jd = cJSON_GetObjectItem(j, "data");
    const char *path = cJSON_IsString(jp) ? jp->valuestring : "/";

    char *dstr = jd ? cJSON_PrintUnformatted(jd) : NULL;
    if (STREAM_CB) STREAM_CB(event, path, dstr ? dstr : "null", STREAM_USER);

    if (dstr) cJSON_free(dstr);
    cJSON_Delete(j);
}

/**
 * Open the stream, following at most `max_hops` 307 redirects.
 * @return an open handle with the headers already read, or NULL.
 */
static esp_http_client_handle_t stream_open(const char *first_url, int max_hops)
{
    char *url = strdup(first_url);
    if (!url) return NULL;

    for (int hop = 0; hop <= max_hops; hop++) {
        esp_http_client_config_t c = {
            .url                   = url,
            .method                = HTTP_METHOD_GET,
            .timeout_ms            = 30000,  /* open phase only; see STREAM_READ_TIMEOUT_MS */
            .crt_bundle_attach     = esp_crt_bundle_attach,
            .buffer_size           = 3072,
            .buffer_size_tx        = 3072,
            .disable_auto_redirect = true,   /* we follow redirects ourselves, see the file header */
        };
        esp_http_client_handle_t h = esp_http_client_init(&c);
        if (!h) break;

        esp_http_client_set_header(h, "Accept", "text/event-stream");

        if (esp_http_client_open(h, 0) != ESP_OK ||
            esp_http_client_fetch_headers(h) < 0) {
            esp_http_client_cleanup(h);
            break;
        }

        int st = esp_http_client_get_status_code(h);

        if (st == 200) {
            free(url);
            /* Lower the timeout only once open: Firebase may take a few seconds to
               return the headers, but after that events must be read at once. */
            esp_http_client_set_timeout_ms(h, STREAM_READ_TIMEOUT_MS);
            ESP_LOGI(TAG, "stream đã mở sau %d lần chuyển hướng", hop);
            return h;
        }

        if (st == 307 || st == 301 || st == 302 || st == 308) {
            char *loc = NULL;
            if (esp_http_client_get_header(h, "Location", &loc) == ESP_OK &&
                loc && loc[0]) {
                char *next = strdup(loc);
                esp_http_client_close(h);
                esp_http_client_cleanup(h);
                if (!next) break;
                free(url);
                url = next;
                continue;      /* go on to the server we were pointed at */
            }
            ESP_LOGE(TAG, "stream: 307 nhưng không có header Location");
        } else {
            ESP_LOGW(TAG, "stream: HTTP %d", st);
        }

        esp_http_client_close(h);
        esp_http_client_cleanup(h);
        break;
    }

    free(url);
    return NULL;
}

static void stream_task(void *arg)
{
    (void)arg;

    char *line  = malloc(STREAM_LINE);
    char *chunk = malloc(STREAM_CHUNK);
    if (!line || !chunk) {
        free(line); free(chunk);
        STREAMER = NULL;
        vTaskDelete(NULL);
        return;
    }

    nht_sse_t sse;
    nht_sse_init(&sse, line, STREAM_LINE, dispatch, NULL);

    while (RUNNING && STREAM_ON) {
        if (!ensure_token()) {
            vTaskDelay(pdMS_TO_TICKS(CFG.retry_ms));
            continue;
        }

        size_t ulen = URL_MAX + TOKEN_MAX;
        char *url = malloc(ulen);
        if (!url) { vTaskDelay(pdMS_TO_TICKS(CFG.retry_ms)); continue; }
        build_url(url, ulen, STREAM_PATH);

        esp_http_client_handle_t h = stream_open(url, 3);
        free(url);

        if (!h) {
            ESP_LOGW(TAG, "không mở được stream, thử lại sau %ums", (unsigned)CFG.retry_ms);
            STREAM_CONNECTED = false;
            set_state(NHT_FB_ERROR);
            vTaskDelay(pdMS_TO_TICKS(CFG.retry_ms));
            continue;
        }

        STREAM_CONNECTED = true;
        set_state(NHT_FB_READY);
        nht_sse_reset(&sse);
        int64_t last_rx_us = esp_timer_get_time();
        /* Tell the listener a snapshot is coming, so it can tell it apart from a real change. */
        if (STREAM_CB) STREAM_CB("connected", "/", "null", STREAM_USER);
        ESP_LOGI(TAG, "đang nghe %s", STREAM_PATH);

        while (RUNNING && STREAM_ON) {
            /* Token about to expire: re-open the stream with a new token instead of
               waiting for the server to cut the connection. */
            if (!token_valid()) {
                ESP_LOGI(TAG, "token sắp hết hạn, mở lại stream");
                break;
            }

            /* esp_http_client_read returns only when `len` bytes are filled OR the
               timeout expires, so a small event waited until the timeout to reach
               us. A 30 s timeout once delayed an open-valve command by half a minute.
               A short timeout bounds the delay; a timeout with no bytes at all is
               normal (EAGAIN), not a dropped connection. */
            int n = esp_http_client_read(h, chunk, STREAM_CHUNK);
            if (n == -ESP_ERR_HTTP_EAGAIN) {
                /* Firebase sends a keep-alive about every 30 s: silent for over 75 s means dead. */
                if (esp_timer_get_time() - last_rx_us > 75LL * 1000000) {
                    ESP_LOGW(TAG, "stream im lặng quá 75 s, mở lại");
                    break;
                }
                continue;
            }
            if (n < 0) break;
            if (n > 0) last_rx_us = esp_timer_get_time();
            if (n == 0) {
                if (esp_http_client_is_complete_data_received(h)) break;
                continue;
            }
            nht_sse_feed(&sse, chunk, (size_t)n);
        }

        STREAM_CONNECTED = false;
        esp_http_client_close(h);
        esp_http_client_cleanup(h);

        if (RUNNING && STREAM_ON) {
            ESP_LOGW(TAG, "stream đứt (đã nhận %u sự kiện, bỏ %u), thử lại sau %ums",
                     (unsigned)sse.events, (unsigned)sse.dropped,
                     (unsigned)CFG.retry_ms);
            vTaskDelay(pdMS_TO_TICKS(CFG.retry_ms));
        }
    }

    free(line);
    free(chunk);
    STREAM_CONNECTED = false;
    STREAMER = NULL;
    vTaskDelete(NULL);
}

esp_err_t nht_fb_stream_start(const char *path, nht_fb_stream_cb_t cb,
                              void *user)
{
    if (STREAMER) return ESP_ERR_INVALID_STATE;

    strncpy(STREAM_PATH, path, sizeof(STREAM_PATH) - 1);
    STREAM_CB   = cb;
    STREAM_USER = user;
    STREAM_ON   = true;

    return xTaskCreate(stream_task, "fb_stream", 8192, NULL, 4, &STREAMER)
               == pdPASS ? ESP_OK : ESP_FAIL;
}

void nht_fb_stream_stop(void) { STREAM_ON = false; }

/* ------------------------------------------------------------------ */

static void cpy(char *dst, size_t cap, const char *src)
{
    strncpy(dst, src ? src : "", cap - 1);
    dst[cap - 1] = '\0';
}

esp_err_t nht_fb_start(const nht_fb_cfg_t *cfg)
{
    if (!cfg || !cfg->api_key || !cfg->db_url || !cfg->email || !cfg->password)
        return ESP_ERR_INVALID_ARG;
    if (RUNNING) return ESP_OK;

    cpy(CFG.api_key,  sizeof(CFG.api_key),  cfg->api_key);
    cpy(CFG.db_url,   sizeof(CFG.db_url),   cfg->db_url);
    cpy(CFG.email,    sizeof(CFG.email),    cfg->email);
    cpy(CFG.password, sizeof(CFG.password), cfg->password);
    CFG.retry_ms = cfg->retry_ms ? cfg->retry_ms : 5000;

    /* Strip a trailing / - otherwise every URL would contain "//". */
    size_t n = strlen(CFG.db_url);
    while (n && CFG.db_url[n - 1] == '/') CFG.db_url[--n] = '\0';

    TOKEN_LOCK = xSemaphoreCreateMutex();
    if (!HTTP_LOCK) HTTP_LOCK = xSemaphoreCreateMutex();
    JOBS = xQueueCreate(cfg->queue_depth ? cfg->queue_depth : 8, sizeof(job_t));
    if (!TOKEN_LOCK || !JOBS) return ESP_ERR_NO_MEM;

    RUNNING = true;
    if (xTaskCreate(worker_task, "fb_rest", 6144, NULL, 4, &WORKER) != pdPASS) {
        RUNNING = false;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Firebase client khởi động, db=%s", CFG.db_url);
    return ESP_OK;
}

void nht_fb_stop(void)
{
    RUNNING   = false;
    STREAM_ON = false;
    set_state(NHT_FB_OFFLINE);
}
