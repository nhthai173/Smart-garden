/**
 * app_cloud.c - Adapter Firebase. Ánh xạ desired/reported.
 *
 * ---------------------------------------------------------------------------
 * VÌ SAO TÁCH desired / reported
 *
 * Hệ cũ dùng chung một nhánh `/data` cho cả lệnh lẫn trạng thái. Thiết bị
 * vừa ghi trạng thái vào đó, vừa nghe realtime chính nhánh đó - nên mỗi lần
 * tự báo cáo là tự nhận lại lệnh của chính mình. Vòng lặp tự dội ấy làm thời
 * lượng tưới bị đặt lại và van đóng sớm.
 *
 * Ở đây hai chiều không giao nhau:
 *
 *     /desired    ứng dụng GHI, thiết bị chỉ ĐỌC   (nghe stream ở đây)
 *     /reported   thiết bị GHI, ứng dụng chỉ ĐỌC   (không bao giờ đọc lại)
 *
 * Thiết bị không thể nghe thấy cái nó vừa ghi - không phải nhờ một cái chốt
 * chặn nào, mà vì hai đường không hề gặp nhau. Cùng mô hình với Device Shadow
 * của AWS IoT, nên sau này chuyển sang cũng không phải nghĩ lại kiến trúc.
 *
 * Luật bảo mật nên khoá luôn ở phía máy chủ:
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

#define REPORT_MIN_INTERVAL_MS 1000
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

/* Ghép BASE + suffix. Dùng bộ đệm riêng đủ rộng cho cả hai phần để trình
   biên dịch không phải đoán, và để tránh cắt cụt đường dẫn - một đường dẫn
   bị cắt sẽ ghi đè nhầm nhánh dữ liệu. */
static void path_of(char *out, size_t len, const char *suffix)
{
    int n = snprintf(out, len, "%s%s", BASE, suffix);
    if (n < 0 || (size_t)n >= len)
        ESP_LOGE(TAG, "đường dẫn quá dài, bị cắt: %s%s", BASE, suffix);
}

/* ------------------------------------------------------------------ */
/* Báo cáo trạng thái  (chỉ ghi, không bao giờ đọc lại)               */
/* ------------------------------------------------------------------ */

static void publish_reported(void)
{
    if (nht_fb_state() != NHT_FB_READY) return;

    water_status_t st;
    water_get_status(&st);

    char ip[16] = "";
    nht_net_ip(ip, sizeof(ip));

    /* Bộ đệm lớn nằm trên heap, không phải stack: hàm này chạy trong task có
       stack 4-6 KB, và snprintf với %f đã tốn hơn 1 KB stack rồi. */
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

/** Người nghe của app_water. Chạy NGAY TRONG task điều khiển (giữ khoá tưới),
 *  nên chỉ được phép làm việc rẻ: đánh dấu rồi đánh thức task báo cáo. Dựng
 *  JSON và đẩy lên mạng ở đây là kéo task điều khiển vào chuyện mạng. */
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

    /* Lịch tưới hiện có: để ứng dụng thấy được thiết bị đang chạy lịch gì,
       kể cả lịch được thêm từ giao diện web cục bộ. */
    water_sched_json(sch, JSON_BUF);
    path_of(p, sizeof(p), "/reported/schedules_list");
    nht_fb_put(p, sch);
    free(json);
    free(sch);
}

/* ------------------------------------------------------------------ */
/* Nhận lệnh từ /desired                                              */
/* ------------------------------------------------------------------ */

static void apply_valve_cmd(cJSON *v)
{
    if (!cJSON_IsObject(v)) return;

    cJSON *jcmd = cJSON_GetObjectItem(v, "cmd");
    cJSON *jid  = cJSON_GetObjectItem(v, "id");
    if (!cJSON_IsString(jcmd)) return;

    /* Chống thực hiện lại lệnh cũ. Khi stream mở lại, Firebase gửi nguyên
       trạng thái hiện tại của /desired - không có bước lọc này thì mỗi lần
       mạng chập chờn là van lại mở thêm một lượt nữa. */
    if (cJSON_IsNumber(jid)) {
        if (jid->valuedouble == last_cmd_id) return;
        last_cmd_id = jid->valuedouble;
    }

    if (!strcmp(jcmd->valuestring, "open")) {
        cJSON *jd = cJSON_GetObjectItem(v, "duration_s");
        cJSON *jl = cJSON_GetObjectItem(v, "level");
        /* Dữ liệu từ cloud là đầu vào không tin cậy: ép kiểu thẳng một số âm
           sang uint32 ra ~4 tỷ giây và van mở hết trần thay vì thời lượng mặc
           định. Số không hợp lệ (<=0, NaN) thì dùng mặc định; số quá lớn để
           water_open kẹp xuống trần. */
        double dd = cJSON_IsNumber(jd) ? jd->valuedouble : 0;
        uint32_t s = (dd >= 1.0) ? (dd > 86400.0 ? 86400u : (uint32_t)dd) : 0u;
        double ld = cJSON_IsNumber(jl) ? jl->valuedouble : 0;
        uint8_t  l = (ld >= 1.0) ? (ld > 100.0 ? 100u : (uint8_t)ld) : 0u;

        /* Không có id thì chỉ thi hành khi nó thực sự đổi trạng thái. */
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
 * @param snapshot  true = bản chụp trạng thái nhận được ngay khi stream (mở
 *                  lại): chỉ ĐỒNG BỘ, không THI HÀNH lệnh nào.
 *
 * Bản chụp mang theo cả lệnh cũ còn nằm trong /desired. Lệnh mở van viết lúc
 * thiết bị mất mạng ba tiếng trước mà thi hành khi có lại mạng thì tưới một
 * lượt không ai mong đợi; lệnh "restart" còn nằm đó thì khởi động lại vô tận.
 * Chỉ thay đổi đến SAU khi stream đã mở mới là lệnh thật.
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
        /* Ghi nhận id của lệnh đang có để sau này không chạy lại nó. */
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
        snapshot_pending = true;      /* put "/" kế tiếp là bản chụp */
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
        snapshot_pending = false;     /* đã có sự kiện cụ thể: hết bản chụp */
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

/** Task báo cáo: gom các thay đổi (tối đa 1 bản tin / REPORT_MIN_INTERVAL_MS)
 *  và gửi nhịp định kỳ. Là nơi DUY NHẤT dựng và đẩy /reported. */
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

    /* Đợi có mạng đã. Không chặn ai: đây là task riêng, van vẫn chạy. */
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

    /* Chờ đăng nhập xong để biết uid, mới dựng được đường dẫn. */
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
