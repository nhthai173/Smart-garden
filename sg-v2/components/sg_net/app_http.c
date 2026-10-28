/**
 * @file    app_http.c
 * @brief   Local API and web UI.
 *
 * This layer is an ADAPTER: it turns HTTP into calls to app_water and nothing more.
 * There is no watering logic here. Remove everything network-related and the
 * device still waters on schedule.
 */

#include "app_http.h"
#include "app_cfg.h"
#include "app_water.h"
#include "app_boot.h"
#include "board_config.h"
#include "version.h"
#include "web_assets.h"

#include "nht_webkit.h"
#include "nht_net.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_idf_version.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "http";

#define BODY_MAX 2048
#define JSON_MAX 2048


int app_info_json(char *buf, size_t len)
{
    char ip[16] = "0.0.0.0";
    nht_net_ip(ip, sizeof(ip));

    return snprintf(buf, len,
        "{\"device\":\"%s\",\"fw\":\"%s\",\"build\":%d,"
        "\"chip\":\"ESP32-C3\",\"idf\":\"%s\","
        "\"ip\":\"%s\",\"rssi\":%d,\"net\":\"%s\","
        "\"heap\":%u,\"heap_min\":%u,\"uptime_s\":%u,"
        "\"last_reset\":\"%s\",\"device_id\":\"%s\"}",
        app_cfg()->dev_name, FIRMWARE_VERSION, FIRMWARE_BUILD,
        IDF_VER, ip, nht_net_rssi(),
        nht_net_state() == NHT_NET_CONNECTED ? "sta"
            : (nht_net_state() == NHT_NET_AP_MODE ? "ap" : "down"),
        (unsigned)esp_get_free_heap_size(),
        (unsigned)esp_get_minimum_free_heap_size(),
        (unsigned)(esp_timer_get_time() / 1000000),
        app_last_reset_str(), app_cfg()->fb_device_id);
}

/* ------------------------------------------------------------------ */
/* Route                                                              */
/* ------------------------------------------------------------------ */

#if defined(SG_BENCH)
void app_bench_register(void);
#endif

static esp_err_t h_index(httpd_req_t *r)
{
    return nht_web_gz(r, web_index_gz, web_index_gz_len,
                      "text/html; charset=utf-8");
}

static esp_err_t h_status(httpd_req_t *r)
{
    char buf[JSON_MAX];
    water_status_json(buf, sizeof(buf));
    return nht_web_json(r, buf);
}

static esp_err_t h_info(httpd_req_t *r)
{
    char buf[JSON_MAX];
    app_info_json(buf, sizeof(buf));
    return nht_web_json(r, buf);
}

static esp_err_t h_water(httpd_req_t *r)
{
    char v[16];

    if (nht_web_query(r, "stop", v, sizeof(v)) >= 0) {
        water_close(WSRC_LOCAL_API);
        return nht_web_json(r, "{\"ok\":true}");
    }

    /* strtol, not strtoul: "-5" through strtoul becomes ~4 billion and the valve
       opens to the ceiling. An invalid number (<=0) takes the default; a large one is left to water_open to clamp. */
    uint32_t seconds = 0;
    uint8_t  level   = 0;
    if (nht_web_query(r, "seconds", v, sizeof(v)) > 0) {
        long n = strtol(v, NULL, 10);
        seconds = n >= 1 ? (n > 86400 ? 86400u : (uint32_t)n) : 0u;
    }
    if (nht_web_query(r, "minutes", v, sizeof(v)) > 0) {
        long n = strtol(v, NULL, 10);
        seconds = n >= 1 ? (n > 1440 ? 86400u : (uint32_t)n * 60u) : 0u;
    }
    if (nht_web_query(r, "level", v, sizeof(v)) > 0) {
        long n = strtol(v, NULL, 10);
        level = n >= 1 ? (n > 100 ? 100u : (uint8_t)n) : 0u;
    }

    nht_valve_err_t err = water_open(seconds, level, WSRC_LOCAL_API);
    if (err == NHT_VALVE_ERR_INHIBIT)
        return nht_web_error(r, 409, "dang co su co an toan (ngap/nguon/cam bien), khong mo van");
    if (err == NHT_VALVE_ERR_BUSY)
        return nht_web_error(r, 409, "van dang dong, doi mot lat");
    if (err != NHT_VALVE_OK)
        return nht_web_error(r, 500, "khong mo duoc van");

    char buf[JSON_MAX];
    water_status_json(buf, sizeof(buf));
    return nht_web_json(r, buf);
}

static esp_err_t h_sched_get(httpd_req_t *r)
{
    char buf[JSON_MAX];
    water_sched_json(buf, sizeof(buf));
    return nht_web_json(r, buf);
}

static esp_err_t h_sched_post(httpd_req_t *r)
{
    char body[BODY_MAX];
    if (nht_web_body(r, body, sizeof(body)) < 0)
        return nht_web_error(r, 400, "than request khong hop le");

    cJSON *j = nht_json_depth_ok(body, 8) ? cJSON_Parse(body) : NULL;
    if (!j) return nht_web_error(r, 400, "JSON hong");

    cJSON *jid  = cJSON_GetObjectItem(j, "id");
    cJSON *jh   = cJSON_GetObjectItem(j, "hour");
    cJSON *jm   = cJSON_GetObjectItem(j, "minute");
    cJSON *jr   = cJSON_GetObjectItem(j, "repeat");
    cJSON *jd   = cJSON_GetObjectItem(j, "duration_s");
    cJSON *jl   = cJSON_GetObjectItem(j, "level");
    cJSON *je   = cJSON_GetObjectItem(j, "enabled");

    if (!cJSON_IsNumber(jh) || !cJSON_IsNumber(jm)) {
        cJSON_Delete(j);
        return nht_web_error(r, 400, "thieu hour/minute");
    }

    uint8_t  hour   = (uint8_t)jh->valueint;
    uint8_t  minute = (uint8_t)jm->valueint;
    uint8_t  repeat = cJSON_IsNumber(jr) ? (uint8_t)jr->valueint
                                         : NHT_SCHED_REPEAT_DAILY;
    uint16_t dur    = cJSON_IsNumber(jd) ? (uint16_t)jd->valueint : 0;
    uint8_t  level  = cJSON_IsNumber(jl) ? (uint8_t)jl->valueint : 100;
    bool     en     = je ? cJSON_IsTrue(je) : true;

    bool ok;
    uint8_t id = 0;
    if (cJSON_IsNumber(jid) && jid->valueint > 0) {
        id = (uint8_t)jid->valueint;
        ok = water_sched_update(id, hour, minute, repeat, dur, level, en);
    } else {
        ok = water_sched_add(hour, minute, repeat, dur, level, en, &id);
    }
    cJSON_Delete(j);

    if (!ok) return nht_web_error(r, 400, "khong luu duoc lich");

    char buf[64];
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"id\":%u}", id);
    return nht_web_json(r, buf);
}

static esp_err_t h_sched_delete(httpd_req_t *r)
{
    char v[8];
    if (nht_web_query(r, "id", v, sizeof(v)) <= 0)
        return nht_web_error(r, 400, "thieu id");

    if (!water_sched_remove((uint8_t)strtoul(v, NULL, 10)))
        return nht_web_error(r, 404, "khong co lich nay");
    return nht_web_json(r, "{\"ok\":true}");
}

static esp_err_t h_config_get(httpd_req_t *r)
{
    char buf[JSON_MAX];
    app_cfg_to_json(buf, sizeof(buf));
    return nht_web_json(r, buf);
}

static esp_err_t h_config_post(httpd_req_t *r)
{
    char body[BODY_MAX];
    if (nht_web_body(r, body, sizeof(body)) < 0)
        return nht_web_error(r, 400, "than request khong hop le");

    cJSON *j = nht_json_depth_ok(body, 8) ? cJSON_Parse(body) : NULL;
    if (!j || !cJSON_IsObject(j)) {
        if (j) cJSON_Delete(j);
        return nht_web_error(r, 400, "JSON hong");
    }

    int applied = 0, rejected = 0;
    cJSON *it;
    cJSON_ArrayForEach(it, j) {
        char val[CFG_URL_LEN];
        if (cJSON_IsString(it))       snprintf(val, sizeof(val), "%s", it->valuestring);
        else if (cJSON_IsNumber(it))  snprintf(val, sizeof(val), "%g", it->valuedouble);
        else if (cJSON_IsBool(it))    snprintf(val, sizeof(val), "%d", cJSON_IsTrue(it));
        else continue;

        if (app_cfg_set_kv(it->string, val)) applied++;
        else { rejected++; ESP_LOGW(TAG, "khoá cấu hình lạ: %s", it->string); }
    }
    cJSON_Delete(j);

    app_cfg_save();
    water_apply_cfg();   /* thresholds, durations and the catch-up window take effect at once */

    char buf[192];
    snprintf(buf, sizeof(buf),
             "{\"ok\":true,\"applied\":%d,\"rejected\":%d,"
             "\"note\":\"doi loai van hoac che do cam bien thi can khoi dong lai\"}",
             applied, rejected);
    return nht_web_json(r, buf);
}

static esp_err_t h_wifi_scan(httpd_req_t *r)
{
    char buf[1536];
    nht_net_scan_json(buf, sizeof(buf));
    return nht_web_json(r, buf);
}

static esp_err_t h_wifi_post(httpd_req_t *r)
{
    char body[512];
    if (nht_web_body(r, body, sizeof(body)) < 0)
        return nht_web_error(r, 400, "than request khong hop le");

    cJSON *j = nht_json_depth_ok(body, 8) ? cJSON_Parse(body) : NULL;
    if (!j) return nht_web_error(r, 400, "JSON hong");

    cJSON *js = cJSON_GetObjectItem(j, "ssid");
    cJSON *jp = cJSON_GetObjectItem(j, "pass");
    if (!cJSON_IsString(js)) {
        cJSON_Delete(j);
        return nht_web_error(r, 400, "thieu ssid");
    }

    app_cfg_set_kv("wifi_ssid", js->valuestring);
    app_cfg_set_kv("wifi_pass", cJSON_IsString(jp) ? jp->valuestring : "");
    app_cfg_save();

    esp_err_t err = nht_net_set_sta(app_cfg()->wifi_ssid, app_cfg()->wifi_pass);
    cJSON_Delete(j);

    if (err != ESP_OK) return nht_web_error(r, 500, "khong doi duoc wifi");
    return nht_web_json(r, "{\"ok\":true,\"message\":\"dang ket noi\"}");
}

static void restart_later(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
}

static esp_err_t h_restart(httpd_req_t *r)
{
    nht_web_json(r, "{\"ok\":true,\"message\":\"dang khoi dong lai\"}");
    xTaskCreate(restart_later, "restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static esp_err_t h_factory_reset(httpd_req_t *r)
{
    /* Close the valve before erasing the settings: after the reset every value
       returns to its default, and the valve should not be left open under the old settings. */
    water_close(WSRC_SAFETY);
    water_sched_clear();
    app_cfg_factory_reset();

    nht_web_json(r, "{\"ok\":true,\"message\":\"da xoa cau hinh, khoi dong lai\"}");
    xTaskCreate(restart_later, "restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* WebSocket: push the status, accept short commands                  */
/* ------------------------------------------------------------------ */

static void on_ws_message(httpd_req_t *r, const char *text, void *user)
{
    (void)r; (void)user;

    if (!strncmp(text, "GET", 3)) {
        water_notify();
    } else if (!strncmp(text, "OPEN", 4)) {
        uint32_t s = 0;
        const char *colon = strchr(text, ':');
        if (colon) s = (uint32_t)strtoul(colon + 1, NULL, 10);
        water_open(s, 0, WSRC_LOCAL_API);
    } else if (!strncmp(text, "CLOSE", 5)) {
        water_close(WSRC_LOCAL_API);
    }
}

static void on_water_change(const water_status_t *st, void *user)
{
    (void)st; (void)user;
    if (nht_web_ws_client_count() == 0) return;

    /* Runs in the control task (small stack): the buffer goes on the heap. */
    char *buf = malloc(JSON_MAX);
    if (!buf) return;
    water_status_json(buf, JSON_MAX);
    nht_web_ws_broadcast(buf);
    free(buf);
}

static void on_ota_done(bool ok, void *user)
{
    (void)user;
    if (!ok) return;
    /* Close the valve before restarting - do not let water run during the reboot. */
    water_close(WSRC_SAFETY);
    xTaskCreate(restart_later, "restart", 2048, NULL, 5, NULL);
}

static void on_ota_progress(size_t done, size_t total, void *user)
{
    (void)user;
    static int last_pct = -1;
    int pct = total ? (int)(done * 100 / total) : 0;
    if (pct / 10 != last_pct / 10) {
        last_pct = pct;
        ESP_LOGI(TAG, "OTA %d%%", pct);
    }
}

/* ------------------------------------------------------------------ */

esp_err_t app_http_start(void)
{
    nht_web_cfg_t cfg = {
        .port            = 80,
        .stack_size      = 8192,
        .max_handlers    = 24,
        .enable_ws       = true,
        .ws_path         = "/ws",
        .enable_ota      = true,
        .ota_path        = "/update",
        .captive         = (nht_net_state() == NHT_NET_AP_MODE),
        .on_ota_done     = on_ota_done,
        .on_ota_progress = on_ota_progress,
    };
    nht_web_ws_on_message(on_ws_message, NULL);

    esp_err_t err = nht_web_start(&cfg);
    if (err != ESP_OK) return err;

    nht_web_route("/",               HTTP_GET,    h_index,         NULL);
    nht_web_route("/api/status",     HTTP_GET,    h_status,        NULL);
    nht_web_route("/api/info",       HTTP_GET,    h_info,          NULL);
    nht_web_route("/api/water",      HTTP_POST,   h_water,         NULL);
    nht_web_route("/api/water",      HTTP_GET,    h_water,         NULL);
    nht_web_route("/api/schedules",  HTTP_GET,    h_sched_get,     NULL);
    nht_web_route("/api/schedules",  HTTP_POST,   h_sched_post,    NULL);
    nht_web_route("/api/schedules",  HTTP_DELETE, h_sched_delete,  NULL);
    nht_web_route("/api/config",     HTTP_GET,    h_config_get,    NULL);
    nht_web_route("/api/config",     HTTP_POST,   h_config_post,   NULL);
    nht_web_route("/api/wifi/scan",  HTTP_GET,    h_wifi_scan,     NULL);
    nht_web_route("/api/wifi",       HTTP_POST,   h_wifi_post,     NULL);
    nht_web_route("/api/restart",    HTTP_POST,   h_restart,       NULL);
    nht_web_route("/api/factory",    HTTP_POST,   h_factory_reset, NULL);
#if defined(SG_BENCH)
    app_bench_register();
#endif

    water_on_change(on_water_change, NULL);

    ESP_LOGI(TAG, "API cục bộ sẵn sàng");
    return ESP_OK;
}
