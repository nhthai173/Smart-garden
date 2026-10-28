/**
 * @file    app_cloud.c
 * @brief   Firebase adapter. Maps /desired and /reported onto the watering core.
 *
 * ---------------------------------------------------------------------------
 * WHY desired AND reported ARE SEPARATE
 *
 * The old system used one `/data` branch for both commands and state. The device
 * wrote its state there and also listened to that same branch in real time - so
 * every self-report came back as a command from itself. That self-echo loop
 * reset the watering duration and closed the valve early.
 *
 * Here the two directions never meet:
 *
 *     /desired    the app WRITES, the device only READS   (it streams this)
 *     /reported   the device WRITES, the app only READS   (never read back)
 *
 * The device cannot hear what it just wrote - not thanks to a guard, but because
 * the two paths never cross. It is the same model as the AWS IoT Device Shadow,
 * so moving there later needs no rethink of the architecture.
 *
 * The security rules should lock this down on the server side as well:
 *     "reported": { ".write": "auth.uid === $uid" }
 *     "desired":  { ".write": "auth.uid === $uid" }
 * ---------------------------------------------------------------------------
 */

#include "app_cloud.h"
#include "app_cfg.h"
#include "app_water.h"
#include "app_boot.h"
#include "app_http.h"
#include "version.h"

#include "nht_fbrtdb.h"
#include "nht_webkit.h"
#include "nht_net.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "cloud";

#define REPORT_MIN_INTERVAL_MS 2000
#define BASE_MAX_LEN            128
#define PATH_MAX_LEN            224
#define JSON_BUF               2048

static char BASE[BASE_MAX_LEN];      /* /<uid>/<device_id> */
static int64_t last_report_us;
static bool    initial_sync_done;
static double  last_cmd_id = -1;
static bool    pending_report;

bool app_cloud_online(void)
{
    return nht_fb_state() == NHT_FB_READY && nht_fb_stream_connected();
}

/* Join BASE + suffix. A dedicated buffer wide enough for both parts spares the
   compiler any guessing and avoids truncating the path - a truncated path would
   overwrite the wrong data branch. */
static void path_of(char *out, size_t len, const char *suffix)
{
    int n = snprintf(out, len, "%s%s", BASE, suffix);
    if (n < 0 || (size_t)n >= len)
        ESP_LOGE(TAG, "đường dẫn quá dài, bị cắt: %s%s", BASE, suffix);
}

/* ------------------------------------------------------------------ */
/* Status report  (write-only, never read back)                     */
/* ------------------------------------------------------------------ */

static void publish_reported(void)
{
    if (nht_fb_state() != NHT_FB_READY) return;

    water_status_t st;
    water_get_status(&st);

    char ip[16] = "";
    nht_net_ip(ip, sizeof(ip));

    /* The large buffer lives on the heap, not the stack: this function runs in a
       task with a 4-6 KB stack, and snprintf with %f already uses over 1 KB of it. */
    char fbuf[40];
    char *json = malloc(JSON_BUF);
    if (!json) return;
    snprintf(json, JSON_BUF,
        "{\"valve\":\"%s\",\"remaining_s\":%u,"
        "\"water_leak\":%s,\"leak_raw\":%u,"
        "\"voltage\":%.2f,\"voltage_low\":%s,"
        "\"flow_lpm\":%.2f,\"flow_total_l\":%.2f,"
        "\"uptime_s\":%u,\"source\":\"%s\",\"schedules\":%u,"
        "\"faults\":%u,\"fault\":\"%s\","
        "\"last_reset\":\"%s\",\"reset_abnormal\":%s,"
        "\"rssi\":%d,\"ip\":\"%s\",\"heap\":%u,\"ts\":%lld}",
        nht_valve_state_str(st.valve), (unsigned)st.remaining_s,
        st.leak ? "true" : "false", st.leak_raw,
        st.voltage, st.voltage_low ? "true" : "false",
        st.flow_lpm, st.flow_total_l,
        (unsigned)st.uptime_s, water_src_str(st.last_src), st.sched_count,
        st.faults, water_faults_str(st.faults, fbuf, sizeof(fbuf)),
        app_last_reset_str(), app_last_reset_abnormal() ? "true" : "false",
        nht_net_rssi(), ip, (unsigned)esp_get_free_heap_size(),
        (long long)(esp_timer_get_time() / 1000000));

    char p[PATH_MAX_LEN];
    path_of(p, sizeof(p), "/reported");
    nht_fb_patch(p, json);
    free(json);

    last_report_us = esp_timer_get_time();
    pending_report = false;
}

static TaskHandle_t BEAT;

/** Listener for app_water. Runs RIGHT INSIDE the control task (holding the watering
 *  lock), so it may only do cheap work: set a flag and wake the report task. Building
 *  JSON and pushing it to the network here would drag the control task into network work. */
static void on_water_change(const water_status_t *st, void *user)
{
    (void)st; (void)user;
    pending_report = true;
    if (BEAT) xTaskNotifyGive(BEAT);
}

static void publish_info(void)
{
    char *json = malloc(JSON_BUF);
    char *sch  = malloc(JSON_BUF);
    if (!json || !sch) { free(json); free(sch); return; }
    app_info_json(json, 512);

    char p[PATH_MAX_LEN];
    path_of(p, sizeof(p), "/info");
    nht_fb_patch(p, json);

    /* The current schedule list, so the app can see what the device runs, including
       schedules added from the local web UI. */
    water_sched_json(sch, JSON_BUF);
    path_of(p, sizeof(p), "/reported/schedules_list");
    nht_fb_put(p, sch);
    free(json);
    free(sch);
}

/* ------------------------------------------------------------------ */
/* Receiving commands from /desired                                   */
/* ------------------------------------------------------------------ */

static void apply_valve_cmd(cJSON *v)
{
    if (!cJSON_IsObject(v)) return;

    cJSON *jcmd = cJSON_GetObjectItem(v, "cmd");
    cJSON *jid  = cJSON_GetObjectItem(v, "id");
    if (!cJSON_IsString(jcmd)) return;

    /* Do not rerun old commands. When the stream re-opens Firebase sends the whole
       current /desired - without this filter every network blip would open the
       valve for another run. */
    if (cJSON_IsNumber(jid)) {
        if (jid->valuedouble == last_cmd_id) return;
        last_cmd_id = jid->valuedouble;
    }

    if (!strcmp(jcmd->valuestring, "open")) {
        cJSON *jd = cJSON_GetObjectItem(v, "duration_s");
        cJSON *jl = cJSON_GetObjectItem(v, "level");
        /* Data from the cloud is untrusted input: casting a negative number straight
           to uint32 gives ~4 billion seconds and the valve opens to the ceiling instead
           of the default duration. An invalid number (<=0, NaN) takes the default; a
           huge one is left for water_open to clamp to the ceiling. */
        double dd = cJSON_IsNumber(jd) ? jd->valuedouble : 0;
        uint32_t s = (dd >= 1.0) ? (dd > 86400.0 ? 86400u : (uint32_t)dd) : 0u;
        double ld = cJSON_IsNumber(jl) ? jl->valuedouble : 0;
        uint8_t  l = (ld >= 1.0) ? (ld > 100.0 ? 100u : (uint8_t)ld) : 0u;

        /* Without an id, act only when it really changes the state. */
        if (!cJSON_IsNumber(jid)) {
            water_status_t st;
            water_get_status(&st);
            if (st.valve != NHT_VALVE_CLOSED) return;
        }
        water_open(s, l, WSRC_CLOUD);

    } else if (!strcmp(jcmd->valuestring, "close")) {
        water_close(WSRC_CLOUD);
    }
}

static void apply_cmd(cJSON *c)
{
    if (!cJSON_IsString(c)) return;

    if (!strcmp(c->valuestring, "restart")) {
        ESP_LOGW(TAG, "cloud yêu cầu khởi động lại");
        water_close(WSRC_SAFETY);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
}

/**
 * @param snapshot  true = a snapshot received right when the stream (re-)opened:
 *                  only SYNCHRONISE, never EXECUTE any command.
 *
 * A snapshot carries the old commands still sitting in /desired. An open command
 * written three hours ago while the device was offline, executed on reconnect,
 * would water when nobody expects it; a "restart" left there would reboot forever.
 * Only changes that arrive AFTER the stream is open are real commands.
 */
static void apply_desired(cJSON *d, bool snapshot)
{
    if (!cJSON_IsObject(d)) return;

    cJSON *sch = cJSON_GetObjectItem(d, "schedules");
    if (cJSON_IsArray(sch)) {
        char *s = cJSON_PrintUnformatted(sch);
        if (s) { water_sched_replace_json(s); cJSON_free(s); }
    }

    cJSON *v = cJSON_GetObjectItem(d, "valve");
    if (snapshot) {
        /* Remember the id of the command that is there so it is never rerun later. */
        cJSON *jid = v ? cJSON_GetObjectItem(v, "id") : NULL;
        if (cJSON_IsNumber(jid)) last_cmd_id = jid->valuedouble;
        ESP_LOGI(TAG, "bản chụp /desired: đồng bộ lịch, bỏ qua lệnh van/cmd cũ (an toàn)");
        return;
    }

    apply_valve_cmd(v);
    apply_cmd(cJSON_GetObjectItem(d, "cmd"));
}

static bool snapshot_pending;

static void on_stream(const char *event, const char *path, const char *data,
                      void *user)
{
    (void)user;

    if (!strcmp(event, "connected")) {
        snapshot_pending = true;      /* the next put "/" is a snapshot */
        return;
    }
    if (!strcmp(event, "auth_revoked")) {
        snapshot_pending = true;
        return;
    }
    if (strcmp(event, "put") && strcmp(event, "patch")) return;

    cJSON *j = nht_json_depth_ok(data, 8) ? cJSON_Parse(data) : NULL;
    if (!j) return;

    if (!strcmp(path, "/")) {
        bool snap = snapshot_pending;
        snapshot_pending = false;
        ESP_LOGI(TAG, "/desired: nhận %s", snap ? "bản chụp" : "cập nhật");
        apply_desired(j, snap);
        if (!initial_sync_done) {
            initial_sync_done = true;
            publish_info();
            publish_reported();
        }
    } else if (!strcmp(path, "/valve")) {
        snapshot_pending = false;     /* a specific event arrived: the snapshot is over */
        apply_valve_cmd(j);
    } else if (!strcmp(path, "/schedules")) {
        char *s = cJSON_PrintUnformatted(j);
        if (s) { water_sched_replace_json(s); cJSON_free(s); }
    } else if (!strcmp(path, "/cmd")) {
        apply_cmd(j);
    }

    cJSON_Delete(j);
}

/* ------------------------------------------------------------------ */

/** Report task: batches changes (at most 1 message per REPORT_MIN_INTERVAL_MS)
 *  and sends a periodic heartbeat. The ONLY place that builds and pushes /reported. */
static void heartbeat_task(void *arg)
{
    (void)arg;
    int ticks = 0;
    while (1) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));
        if (nht_fb_state() != NHT_FB_READY) continue;

        int64_t since = esp_timer_get_time() - last_report_us;
        if (pending_report) {
            if (since < REPORT_MIN_INTERVAL_MS * 1000LL) {
                vTaskDelay(pdMS_TO_TICKS(
                    REPORT_MIN_INTERVAL_MS - (uint32_t)(since / 1000)));
            }
            publish_reported();
            ticks = 0;
        } else if (++ticks >= 30) {
            ticks = 0;
            publish_reported();
        }
    }
}

static void on_fb_state(nht_fb_state_t st, void *user)
{
    (void)user;
    if (st == NHT_FB_READY && BASE[0] == '\0' && nht_fb_uid()[0]) {
        snprintf(BASE, sizeof(BASE), "/%s/%s",
                 nht_fb_uid(), app_cfg()->fb_device_id);
        ESP_LOGI(TAG, "nhánh dữ liệu: %s", BASE);
    }
}

static void start_task(void *arg)
{
    (void)arg;
    app_cfg_t *c = app_cfg();

    /* Wait for the network first. Nobody is blocked: this is its own task, the valve keeps running. */
    while (nht_net_state() != NHT_NET_CONNECTED)
        vTaskDelay(pdMS_TO_TICKS(1000));

    nht_fb_cfg_t fc = {
        .api_key     = c->fb_api_key,
        .db_url      = c->fb_db_url,
        .email       = c->fb_email,
        .password    = c->fb_pass,
        .queue_depth = 8,
        .retry_ms    = 5000,
    };
    nht_fb_on_state(on_fb_state, NULL);

    if (nht_fb_start(&fc) != ESP_OK) {
        ESP_LOGE(TAG, "không khởi động được Firebase client");
        vTaskDelete(NULL);
        return;
    }

    /* Wait for the sign-in to know the uid before the path can be built. */
    for (int i = 0; i < 120 && BASE[0] == '\0'; i++)
        vTaskDelay(pdMS_TO_TICKS(1000));

    if (BASE[0] == '\0') {
        ESP_LOGE(TAG, "không đăng nhập được Firebase, chạy tiếp ở chế độ cục bộ");
        vTaskDelete(NULL);
        return;
    }

    char p[PATH_MAX_LEN];
    path_of(p, sizeof(p), "/desired");
    nht_fb_stream_start(p, on_stream, NULL);

    water_on_change(on_water_change, NULL);
    xTaskCreate(heartbeat_task, "fb_beat", 6144, NULL, 3, &BEAT);

    vTaskDelete(NULL);
}

esp_err_t app_cloud_start(void)
{
    if (!app_cfg_firebase_ready()) {
        ESP_LOGI(TAG, "chưa cấu hình Firebase - chỉ chạy cục bộ");
        return ESP_ERR_INVALID_STATE;
    }
    return xTaskCreate(start_task, "cloud_init", 4096, NULL, 3, NULL) == pdPASS
               ? ESP_OK : ESP_FAIL;
}
