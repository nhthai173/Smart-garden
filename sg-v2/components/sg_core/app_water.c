#include "app_water.h"
#include "app_cfg.h"
#include "board_config.h"

#include "nht_dinput.h"
#include "nht_adc.h"
#include "nht_pulse.h"
#include "nht_store.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "driver/gpio.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "water";

#define WATER_MAX_LISTENERS 4
/* Vòng điều khiển KHÔNG quay đều theo nhịp: nó ngủ cho tới khi có sự kiện
   (lệnh, hạn của van, một trong ba timer lấy mẫu dưới đây). */
#define LEAK_SAMPLE_MS      200
#define SLOW_SAMPLE_MS     1000   /* điện áp; lưu lượng cứ 5 lần một lần   */
#define SCHED_CHECK_MS     1000
#define MAX_SLEEP_MS       1000   /* trần ngủ: còn phải nuôi watchdog      */

#define EV_CMD    (1u << 0)       /* có lệnh/đổi cấu hình: tính lại hạn   */
#define EV_LEAK   (1u << 1)
#define EV_SLOW   (1u << 2)
#define EV_SCHED  (1u << 3)

/* Chống nhiễu cho đường an toàn: sự cố cảm biến/nguồn phải lặp lại mới tính,
   và phải ổn định mới được gỡ - không để một cú nhiễu đóng/mở van liên tục. */
#define LEAK_FAIL_MAX       3     /* lần đọc ADC hỏng liên tiếp            */
#define LEAK_RECOVER_OK     5     /* lần đọc tốt liên tiếp để gỡ           */
#define VOLT_BAD_N          3     /* mẫu thấp liên tiếp (3 s)              */
#define VOLT_GOOD_N         5     /* mẫu tốt liên tiếp để gỡ               */
#define VOLT_HYST_V         0.3f

NHT_SCHED_ASSERT_PAYLOAD(water_task_t);

/* Lịch tưới nằm cùng partition với cấu hình nhưng khác namespace. */
#define SCHED_PARTITION "nvs_cfg"
#define SCHED_NAMESPACE "sched"
#define SCHED_KEY       "tasks"

static nht_valve_t   V;
static nht_dinput_t  LEAK;
static nht_adc_t     LEAK_ADC;
static nht_adc_t     VOLT_ADC;
static nht_pulse_t  *FLOW;
static nht_sched_t   SCHED;
static nht_store_t   SCHED_ST;

static SemaphoreHandle_t LOCK;
static water_status_t    ST;
static water_src_t       LAST_SRC = WSRC_BOOT;
static bool              leak_adc_ok, volt_adc_ok;
static int64_t           boot_us;

static TaskHandle_t      CTL;
static esp_timer_handle_t T_LEAK, T_SLOW, T_SCHED;
static uint8_t           FAULTS;
static uint8_t           leak_fail, leak_good;
static uint8_t           volt_bad, volt_good;
static uint8_t           slow_n;
#if defined(SG_BENCH)
static volatile bool     bench_leak_fail;
static volatile uint32_t bench_stall_ms;
#endif

static struct {
    water_change_cb_t cb;
    void *user;
} LISTENERS[WATER_MAX_LISTENERS];

static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void lock(void)   { xSemaphoreTakeRecursive(LOCK, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGiveRecursive(LOCK); }

const char *water_src_str(water_src_t s)
{
    switch (s) {
    case WSRC_BOOT:      return "boot";
    case WSRC_LOCAL_API: return "api";
    case WSRC_SCHEDULE:  return "schedule";
    case WSRC_CLOUD:     return "cloud";
    case WSRC_BUTTON:    return "button";
    case WSRC_SAFETY:    return "safety";
    default:             return "?";
    }
}

/* ------------------------------------------------------------------ */
/* Trạng thái + thông báo                                             */
/* ------------------------------------------------------------------ */

static void refresh_status(void)
{
    uint32_t t = now_ms();
    ST.valve       = nht_valve_state(&V);
    ST.remaining_s = nht_valve_remaining_ms(&V, t) / 1000u;
    ST.leak        = nht_dinput_active(&LEAK);
    ST.leak_raw    = LEAK.last_raw;
    ST.uptime_s    = (uint32_t)((esp_timer_get_time() - boot_us) / 1000000);
    ST.last_src    = LAST_SRC;
    ST.sched_count = SCHED.count;
    ST.faults      = FAULTS;

    time_t nowt = time(NULL);
    ST.time_valid = (nowt > 1700000000);
}

/** Đánh thức vòng điều khiển để nó tính lại hạn kế tiếp. Gọi từ task nào cũng được. */
static void poke(uint32_t ev)
{
    if (CTL) xTaskNotify(CTL, ev, eSetBits);
}

const char *water_faults_str(uint8_t f, char *buf, size_t len)
{
    snprintf(buf, len, "%s%s%s%s%s",
             (f & WF_LEAK) ? "leak" : "",
             ((f & WF_LEAK) && (f & ~WF_LEAK)) ? "+" : "",
             (f & WF_LEAK_SENSOR) ? "leak_sensor" : "",
             ((f & WF_LEAK_SENSOR) && (f & WF_VOLTAGE)) ? "+" : "",
             (f & WF_VOLTAGE) ? "voltage" : "");
    return buf;
}

void water_notify(void)
{
    water_status_t snap;
    lock();
    refresh_status();
    snap = ST;
    unlock();

    for (int i = 0; i < WATER_MAX_LISTENERS; i++)
        if (LISTENERS[i].cb) LISTENERS[i].cb(&snap, LISTENERS[i].user);
}

bool water_on_change(water_change_cb_t cb, void *user)
{
    for (int i = 0; i < WATER_MAX_LISTENERS; i++) {
        if (!LISTENERS[i].cb) {
            LISTENERS[i].cb   = cb;
            LISTENERS[i].user = user;
            return true;
        }
    }
    return false;
}

void water_get_status(water_status_t *out)
{
    lock();
    refresh_status();
    *out = ST;
    unlock();
}

/* ------------------------------------------------------------------ */
/* Lưu / nạp lịch tưới                                                */
/* ------------------------------------------------------------------ */

static void sched_persist(void)
{
    esp_err_t err = nht_store_set_blob(&SCHED_ST, SCHED_KEY, &SCHED,
                                       sizeof(SCHED));
    if (err != ESP_OK)
        ESP_LOGE(TAG, "không ghi được lịch: %s", esp_err_to_name(err));
}

static void sched_restore(void)
{
    nht_sched_init(&SCHED, app_cfg()->catchup_window_s);

    size_t len = sizeof(SCHED);
    nht_sched_t tmp;
    size_t want = sizeof(tmp);
    esp_err_t err = nht_store_get_blob(&SCHED_ST, SCHED_KEY, &tmp, &want);

    if (err == ESP_OK && want == sizeof(tmp)) {
        SCHED = tmp;
        /* Cửa sổ bù có thể đã đổi trong cấu hình kể từ lần ghi. */
        SCHED.catchup_window_s = app_cfg()->catchup_window_s;
        ESP_LOGI(TAG, "nạp %u lịch tưới từ NVS", SCHED.count);
    } else {
        ESP_LOGI(TAG, "chưa có lịch nào trong NVS");
    }
    (void)len;
}

/* ------------------------------------------------------------------ */
/* Sự kiện van                                                        */
/* ------------------------------------------------------------------ */

static void on_valve_event(nht_valve_t *v, nht_valve_event_t ev, void *user)
{
    (void)v; (void)user;
    switch (ev) {
    case NHT_VALVE_EV_OPENED:
        ESP_LOGI(TAG, "van MỞ (%s), còn %us",
                 water_src_str(LAST_SRC), (unsigned)(nht_valve_remaining_ms(&V, now_ms()) / 1000));
        break;
    case NHT_VALVE_EV_CLOSED:
        ESP_LOGI(TAG, "van ĐÓNG (%s)", water_src_str(LAST_SRC));
        break;
    case NHT_VALVE_EV_AUTO_CLOSED:
        ESP_LOGI(TAG, "van ĐÓNG - hết thời lượng");
        break;
    case NHT_VALVE_EV_INHIBITED:
        ESP_LOGW(TAG, "van ĐÓNG - khoá an toàn (ngập nước)");
        break;
    }
    water_notify();
}

static void eval_faults(uint32_t t);

static void on_leak_change(nht_dinput_t *in, nht_din_state_t st, void *user)
{
    (void)in; (void)user; (void)st;
    eval_faults(now_ms());      /* đọc lại trạng thái cảm biến, đóng van nếu cần */
}

/* Gộp MỌI nguồn sự cố về một chỗ. Có bit nào bật là gửi lệnh ĐÓNG ngay trong
   cùng lần gọi - không đợi chu kỳ sau, không đợi tick. */
static void eval_faults(uint32_t t)
{
    uint8_t m = 0;
    if (nht_dinput_active(&LEAK))                                   m |= WF_LEAK;
    if (app_cfg()->leak_analog &&
        (!leak_adc_ok || leak_fail >= LEAK_FAIL_MAX))               m |= WF_LEAK_SENSOR;
    if (volt_adc_ok && volt_bad >= VOLT_BAD_N)                      m |= WF_VOLTAGE;

    if (m == FAULTS) return;

    uint8_t rising = m & (uint8_t)~FAULTS;
    char b[40];
    if (rising) {
        LAST_SRC = WSRC_SAFETY;
        ESP_LOGW(TAG, "SỰ CỐ [%s] - ĐÓNG VAN, khoá mở (nguồn %.2fV, leak raw %u)",
                 water_faults_str(m, b, sizeof(b)), ST.voltage, LEAK.last_raw);
    } else {
        ESP_LOGI(TAG, "sự cố còn lại [%s]%s", water_faults_str(m, b, sizeof(b)),
                 m ? "" : " - đã hết, mở khoá van");
    }
    FAULTS = m;

    nht_valve_set_inhibit(&V, m != 0, t);
    /* Đai và dây lưng: nếu vì lý do gì đó van chưa nằm trên đường đóng thì
       ép đóng bất chấp mọi ràng buộc. */
    if (rising && nht_valve_state(&V) != NHT_VALVE_CLOSED &&
        nht_valve_state(&V) != NHT_VALVE_CLOSING)
        nht_valve_close_force(&V, t);
    water_notify();
}

/* ------------------------------------------------------------------ */
/* Lệnh                                                               */
/* ------------------------------------------------------------------ */

nht_valve_err_t water_open(uint32_t seconds, uint8_t level, water_src_t src)
{
    lock();
    app_cfg_t *c = app_cfg();
    if (seconds == 0) seconds = c->run_default_s;
    if (seconds > c->run_max_s) seconds = c->run_max_s;

    LAST_SRC = src;
    nht_valve_err_t err = nht_valve_open(&V, seconds * 1000u, level, now_ms());
    unlock();

    poke(EV_CMD);
    if (err != NHT_VALVE_OK)
        ESP_LOGW(TAG, "từ chối mở van (%s): mã %d", water_src_str(src), err);
    else
        ESP_LOGI(TAG, "mở van %us mức %u%% (%s)",
                 (unsigned)seconds, level ? level : 100, water_src_str(src));
    water_notify();
    return err;
}

nht_valve_err_t water_close(water_src_t src)
{
    lock();
    LAST_SRC = src;
    nht_valve_err_t err = nht_valve_close(&V, now_ms());
    unlock();
    poke(EV_CMD);
    water_notify();
    return err;
}

/* ------------------------------------------------------------------ */
/* CRUD lịch                                                          */
/* ------------------------------------------------------------------ */

bool water_sched_add(uint8_t hour, uint8_t minute, uint8_t repeat_mask,
                     uint16_t duration_s, uint8_t level, bool enabled,
                     uint8_t *out_id)
{
    nht_sched_task_t t;
    memset(&t, 0, sizeof(t));
    t.hour = hour; t.minute = minute;
    t.repeat_mask = repeat_mask ? repeat_mask : NHT_SCHED_REPEAT_DAILY;
    t.enabled = enabled;

    water_task_t p = {
        .duration_s = duration_s ? duration_s : (uint16_t)app_cfg()->run_default_s,
        .level_pct  = level ? level : 100,
    };
    nht_sched_set_payload(&t, &p, sizeof(p));

    lock();
    bool ok = nht_sched_add(&SCHED, &t, out_id);
    if (ok) sched_persist();
    unlock();
    return ok;
}

bool water_sched_update(uint8_t id, uint8_t hour, uint8_t minute,
                        uint8_t repeat_mask, uint16_t duration_s,
                        uint8_t level, bool enabled)
{
    lock();
    nht_sched_task_t *cur = nht_sched_find(&SCHED, id);
    if (!cur) { unlock(); return false; }

    nht_sched_task_t t = *cur;
    t.hour = hour; t.minute = minute;
    t.repeat_mask = repeat_mask ? repeat_mask : NHT_SCHED_REPEAT_DAILY;
    t.enabled = enabled;

    water_task_t p = {
        .duration_s = duration_s ? duration_s : (uint16_t)app_cfg()->run_default_s,
        .level_pct  = level ? level : 100,
    };
    nht_sched_set_payload(&t, &p, sizeof(p));

    bool ok = nht_sched_update(&SCHED, &t);
    if (ok) sched_persist();
    unlock();
    return ok;
}

bool water_sched_remove(uint8_t id)
{
    lock();
    bool ok = nht_sched_remove(&SCHED, id);
    if (ok) sched_persist();
    unlock();
    return ok;
}

void water_sched_clear(void)
{
    lock();
    nht_sched_clear(&SCHED);
    SCHED.catchup_window_s = app_cfg()->catchup_window_s;
    sched_persist();
    unlock();
}

int water_sched_json(char *buf, size_t len)
{
    int n = snprintf(buf, len, "[");
    lock();
    bool first = true;
    for (int i = 0; i < NHT_SCHED_MAX_TASKS && n < (int)len; i++) {
        nht_sched_task_t *t = &SCHED.tasks[i];
        if (t->id == 0) continue;

        water_task_t p;
        memcpy(&p, t->payload, sizeof(p));
        n += snprintf(buf + n, len - n,
                      "%s{\"id\":%u,\"hour\":%u,\"minute\":%u,\"repeat\":%u,"
                      "\"duration_s\":%u,\"level\":%u,\"enabled\":%s}",
                      first ? "" : ",",
                      t->id, t->hour, t->minute, t->repeat_mask,
                      p.duration_s, p.level_pct,
                      t->enabled ? "true" : "false");
        first = false;
    }
    unlock();
    n += snprintf(buf + n, len > (size_t)n ? len - n : 0, "]");
    return n;
}

bool water_sched_replace_json(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;
    if (!cJSON_IsArray(root)) { cJSON_Delete(root); return false; }

    lock();
    /* Giữ lại last_run_day của các lịch trùng giờ để đồng bộ từ cloud không
       làm lịch đã chạy sáng nay chạy lại lần nữa. */
    nht_sched_t old = SCHED;
    nht_sched_clear(&SCHED);
    SCHED.catchup_window_s = app_cfg()->catchup_window_s;

    cJSON *it;
    cJSON_ArrayForEach(it, root) {
        cJSON *jh = cJSON_GetObjectItem(it, "hour");
        cJSON *jm = cJSON_GetObjectItem(it, "minute");
        if (!cJSON_IsNumber(jh) || !cJSON_IsNumber(jm)) continue;

        cJSON *jr  = cJSON_GetObjectItem(it, "repeat");
        cJSON *jd  = cJSON_GetObjectItem(it, "duration_s");
        cJSON *jl  = cJSON_GetObjectItem(it, "level");
        cJSON *je  = cJSON_GetObjectItem(it, "enabled");

        nht_sched_task_t t;
        memset(&t, 0, sizeof(t));
        t.hour        = (uint8_t)jh->valueint;
        t.minute      = (uint8_t)jm->valueint;
        t.repeat_mask = cJSON_IsNumber(jr) ? (uint8_t)jr->valueint
                                           : NHT_SCHED_REPEAT_DAILY;
        t.enabled     = je ? cJSON_IsTrue(je) : true;

        water_task_t p = {
            .duration_s = cJSON_IsNumber(jd) ? (uint16_t)jd->valueint
                                             : (uint16_t)app_cfg()->run_default_s,
            .level_pct  = cJSON_IsNumber(jl) ? (uint8_t)jl->valueint : 100,
        };
        nht_sched_set_payload(&t, &p, sizeof(p));

        for (int i = 0; i < NHT_SCHED_MAX_TASKS; i++) {
            if (old.tasks[i].id && old.tasks[i].hour == t.hour &&
                old.tasks[i].minute == t.minute) {
                t.last_run_day = old.tasks[i].last_run_day;
                break;
            }
        }

        uint8_t id;
        if (nht_sched_add(&SCHED, &t, &id))
            nht_sched_find(&SCHED, id)->last_run_day = t.last_run_day;
    }

    sched_persist();
    unlock();

    cJSON_Delete(root);
    ESP_LOGI(TAG, "đồng bộ %u lịch từ cloud", SCHED.count);
    water_notify();
    return true;
}

int water_status_json(char *buf, size_t len)
{
    water_status_t s;
    char fbuf[40];
    water_get_status(&s);
    return snprintf(buf, len,
        "{\"valve\":\"%s\",\"remaining_s\":%u,"
        "\"water_leak\":%s,\"leak_raw\":%u,"
        "\"voltage\":%.2f,\"voltage_low\":%s,"
        "\"flow_lpm\":%.2f,\"flow_total_l\":%.2f,"
        "\"uptime_s\":%u,\"source\":\"%s\",\"schedules\":%u,"
        "\"time_valid\":%s,\"faults\":%u,\"fault\":\"%s\"}",
        nht_valve_state_str(s.valve), (unsigned)s.remaining_s,
        s.leak ? "true" : "false", s.leak_raw,
        s.voltage, s.voltage_low ? "true" : "false",
        s.flow_lpm, s.flow_total_l,
        (unsigned)s.uptime_s, water_src_str(s.last_src), s.sched_count,
        s.time_valid ? "true" : "false",
        s.faults, water_faults_str(s.faults, fbuf, sizeof(fbuf)));
}

/* ------------------------------------------------------------------ */
/* Vòng điều khiển                                                    */
/* ------------------------------------------------------------------ */

static void read_leak(uint32_t t)
{
    uint16_t raw = 0;
    if (app_cfg()->leak_analog) {
        if (!leak_adc_ok) return;      /* WF_LEAK_SENSOR đã bật từ eval_faults */
        esp_err_t err = ESP_OK;
#if defined(SG_BENCH)
        if (bench_leak_fail) err = ESP_FAIL;
        else
#endif
        err = nht_adc_read_raw(&LEAK_ADC, &raw);

        if (err != ESP_OK) {
            /* Cảm biến hỏng KHÔNG được im lặng bỏ qua: không đọc được nghĩa là
               không còn biết có ngập hay không. */
            if (leak_fail < 255) leak_fail++;
            leak_good = 0;
            return;
        }
        if (leak_fail && ++leak_good >= LEAK_RECOVER_OK) {
            leak_fail = 0; leak_good = 0;
        }
    } else {
        raw = (uint16_t)gpio_get_level(PIN_LEAK_DIGITAL);
    }
    nht_dinput_feed(&LEAK, raw, t);
}

static void read_slow(uint32_t t)
{
    if (volt_adc_ok) {
        float v;
        float vmin = app_cfg()->volt_min;
        if (nht_adc_read_value(&VOLT_ADC, &v) == ESP_OK) {
            ST.voltage = v;
            if (vmin > 0 && v < vmin) {
                if (volt_bad < 255) volt_bad++;
                volt_good = 0;
            } else if (v >= vmin + VOLT_HYST_V) {
                if (volt_bad && ++volt_good >= VOLT_GOOD_N) {
                    volt_bad = 0; volt_good = 0;
                }
            }
        } else {
            /* Không đo được nguồn thì không dám chạy motor. */
            if (volt_bad < 255) volt_bad++;
            volt_good = 0;
        }
        ST.voltage_low = (volt_bad >= VOLT_BAD_N);
    }
    if (FLOW && ++slow_n >= 5) {
        slow_n = 0;
        ST.flow_lpm     = nht_pulse_rate(FLOW, t);
        ST.flow_total_l = nht_pulse_volume(FLOW);
    }
}

static void check_schedule(void)
{
    time_t nowt = time(NULL);
    if (nowt < 1700000000) return;    /* chưa đồng bộ SNTP */

    nht_time_t lt;
    nht_time_from_unix((int64_t)nowt, app_cfg()->tz_offset_s, &lt);

    nht_sched_task_t due;
    uint8_t id;
    while ((id = nht_sched_due(&SCHED, &lt, &due)) != 0) {
        water_task_t p;
        memcpy(&p, due.payload, sizeof(p));
        ESP_LOGI(TAG, "lịch #%u tới hạn (%02u:%02u) -> tưới %us mức %u%%",
                 id, due.hour, due.minute, p.duration_s, p.level_pct);
        sched_persist();          /* ghi lại last_run_day */
        water_open(p.duration_s, p.level_pct, WSRC_SCHEDULE);
    }
}

/* Callback của esp_timer chạy trong task esp_timer: chỉ đánh thức, không xử lý. */
static void on_timer(void *arg)
{
    poke((uint32_t)(uintptr_t)arg);
}

/**
 * Vòng điều khiển theo SỰ KIỆN. Nó chỉ thức dậy khi:
 *   - có lệnh hoặc đổi cấu hình (EV_CMD),
 *   - tới hạn của van (hết hành trình, hết thời lượng, hết trễ bơm...),
 *   - một timer lấy mẫu nổ (cảm biến ngập, nguồn, lịch).
 * Không có việc thì ngủ, tối đa MAX_SLEEP_MS để còn nuôi watchdog.
 */
static void ctl_task(void *arg)
{
    (void)arg;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    nht_valve_state_t prev = nht_valve_state(&V);
    uint32_t pending = EV_LEAK | EV_SLOW | EV_SCHED;    /* vòng đầu: đọc hết */

    while (1) {
        lock();
        uint32_t t = now_ms();
        nht_valve_tick(&V, t);

        if (pending & EV_LEAK)  read_leak(t);
        if (pending & EV_SLOW)  read_slow(t);
        if (pending & EV_SCHED) check_schedule();
        eval_faults(t);

        nht_valve_state_t cur = nht_valve_state(&V);
        uint32_t wait_ms = MAX_SLEEP_MS, d;
        if (nht_valve_next_wake_ms(&V, t, &d) && d < wait_ms) wait_ms = d;
        unlock();

        if (cur != prev) {
            prev = cur;
            water_notify();
        }

        esp_task_wdt_reset();
#if defined(SG_BENCH)
        if (bench_stall_ms) {          /* mô phỏng task treo cứng */
            uint32_t ms = bench_stall_ms; bench_stall_ms = 0;
            ESP_LOGW(TAG, "BENCH: treo ctl %u ms", (unsigned)ms);
            vTaskDelay(pdMS_TO_TICKS(ms));
        }
#endif
        /* +1 tick: pdMS_TO_TICKS làm tròn xuống, 0 tick = quay vòng vô ích. */
        uint32_t got = 0;
        xTaskNotifyWait(0, UINT32_MAX, &got, pdMS_TO_TICKS(wait_ms) + 1);
        pending = got;
    }
}

/* ------------------------------------------------------------------ */
/* Khởi tạo                                                           */
/* ------------------------------------------------------------------ */

esp_err_t water_init(void)
{
    app_cfg_t *c = app_cfg();
    boot_us = esp_timer_get_time();
    LOCK = xSemaphoreCreateRecursiveMutex();
    if (!LOCK) return ESP_ERR_NO_MEM;

    memset(&ST, 0, sizeof(ST));

    /* --- van --- */
    nht_valve_cfg_t vc = {
        .type           = c->valve_type ? NHT_VALVE_BALL : NHT_VALVE_SOLENOID,
        .pin_power      = PIN_VALVE_POWER,
        .pin_dir        = c->valve_type ? PIN_VALVE_DIRECTION : -1,
        .pin_pump       = PIN_PUMP_POWER,
        .pin_ac         = PIN_AC_POWER,
        .active_high    = (RELAY_ACTIVE_LEVEL != 0),
        .travel_ms      = c->travel_ms,
        .pump_delay_ms  = c->pump_delay_ms,
        .ac_auto_off_ms = c->ac_auto_off_ms,
        .default_run_ms = c->run_default_s * 1000u,
        .max_run_ms     = c->run_max_s * 1000u,
    };
    nht_valve_init(&V, &vc, now_ms());
    nht_valve_on_event(&V, on_valve_event, NULL);

    /* --- cảm biến ngập --- */
    memset(&LEAK, 0, sizeof(LEAK));
    LEAK.analog      = c->leak_analog;
    LEAK.inverted    = c->leak_inverted;
    LEAK.thr_on      = c->leak_thr_on;
    LEAK.thr_off     = c->leak_thr_off;
    LEAK.hold_on_ms  = c->leak_hold_on_ms;
    LEAK.hold_off_ms = c->leak_hold_off_ms;
    LEAK.on_change   = on_leak_change;
    nht_dinput_init(&LEAK, NHT_DIN_INACTIVE);

    if (c->leak_analog) {
        nht_adc_cfg_t ac = { .gpio = PIN_LEAK_ADC, .samples = 5 };
        leak_adc_ok = (nht_adc_init(&LEAK_ADC, &ac) == ESP_OK);
    } else {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << PIN_LEAK_DIGITAL,
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
        };
        gpio_config(&io);
    }

    /* --- điện áp nguồn --- */
    nht_adc_cfg_t vac = {
        .gpio    = PIN_VOLTAGE_ADC,
        .samples = 5,
        .scale   = c->volt_scale,
    };
    volt_adc_ok = (nht_adc_init(&VOLT_ADC, &vac) == ESP_OK);

    /* --- lưu lượng --- */
    nht_pulse_cfg_t pc = {
        .gpio            = PIN_FLOW_SENSOR,
        .pulses_per_unit = c->flow_pulses_per_l,
        .pullup          = true,
    };
    if (nht_pulse_init(&FLOW, &pc) != ESP_OK) FLOW = NULL;

    /* --- lịch: nạp NGAY, trước cả khi có WiFi --- */
    if (nht_store_init(&SCHED_ST, SCHED_PARTITION, SCHED_NAMESPACE) == ESP_OK)
        sched_restore();
    else
        nht_sched_init(&SCHED, c->catchup_window_s);

    ESP_LOGI(TAG, "sẵn sàng: van=%s leak=%s flow=%s",
             c->valve_type ? "bi" : "solenoid",
             c->leak_analog ? (leak_adc_ok ? "analog" : "analog(LỖI)") : "digital",
             FLOW ? "có" : "không");
    return ESP_OK;
}

void water_apply_cfg(void)
{
    app_cfg_t *c = app_cfg();

    lock();
    /* Cảm biến ngập: chỉ sửa tham số, KHÔNG gọi lại init - init sẽ xoá cả
       trạng thái đang có, và đang báo ngập mà bị xoá thì van mở lại ngay. */
    if (c->leak_thr_off < c->leak_thr_on) {
        LEAK.thr_on  = c->leak_thr_on;
        LEAK.thr_off = c->leak_thr_off;
    }
    LEAK.hold_on_ms  = c->leak_hold_on_ms;
    LEAK.hold_off_ms = c->leak_hold_off_ms;
    LEAK.inverted    = c->leak_inverted;

    V.cfg.travel_ms      = c->travel_ms;
    V.cfg.pump_delay_ms  = c->pump_delay_ms;
    V.cfg.default_run_ms = c->run_default_s * 1000u;
    V.cfg.max_run_ms     = c->run_max_s * 1000u;
    V.pump.cfg.on_delay_ms = c->pump_delay_ms;
    V.ac.cfg.auto_off_ms   = c->ac_auto_off_ms;

    SCHED.catchup_window_s = c->catchup_window_s;
    unlock();

    poke(EV_CMD | EV_LEAK);
    ESP_LOGI(TAG, "đã áp dụng cấu hình mới (không cần khởi động lại)");
    water_notify();
}

#if defined(SG_BENCH)
void water_bench_start_trace(void);
#endif

static esp_err_t mk_timer(esp_timer_handle_t *h, const char *name,
                          uint32_t ev, uint32_t period_ms)
{
    esp_timer_create_args_t a = {
        .callback = on_timer, .arg = (void *)(uintptr_t)ev, .name = name,
    };
    esp_err_t err = esp_timer_create(&a, h);
    if (err == ESP_OK) err = esp_timer_start_periodic(*h, period_ms * 1000ULL);
    return err;
}

esp_err_t water_start(void)
{
    BaseType_t ok = xTaskCreate(ctl_task, "ctl", 6144, NULL, 6, &CTL);
    if (ok != pdPASS) return ESP_FAIL;

    esp_err_t err = mk_timer(&T_LEAK, "w_leak", EV_LEAK, LEAK_SAMPLE_MS);
    if (err == ESP_OK) err = mk_timer(&T_SLOW, "w_slow", EV_SLOW, SLOW_SAMPLE_MS);
    if (err == ESP_OK) err = mk_timer(&T_SCHED, "w_sched", EV_SCHED, SCHED_CHECK_MS);
#if defined(SG_BENCH)
    water_bench_start_trace();
#endif
    return err;
}

#if defined(SG_BENCH)
#include "soc/gpio_reg.h"
#include "soc/soc.h"

void water_bench_set_leak_raw(uint16_t raw) { nht_adc_fake_set(PIN_LEAK_ADC, raw); }
void water_bench_set_volt_raw(uint16_t raw) { nht_adc_fake_set(PIN_VOLTAGE_ADC, raw); }
void water_bench_fail_leak_adc(bool fail)   { bench_leak_fail = fail; }
void water_bench_stall_ctl(uint32_t ms)     { bench_stall_ms = ms; poke(EV_CMD); }

/* Ghi lại MỌI lần relay đổi mức, kèm mốc thời gian của chip (độ phân giải
   5 ms), ra serial. Bộ test dựng lại trình tự và thời gian từ đây - không dựa
   vào WiFi/HTTP nên không méo khi sóng yếu. */
static QueueHandle_t TRACE_Q;
static uint32_t      trace_last = 0xFFFFFFFFu;

static uint32_t pins_active_mask(void)
{
    uint32_t out = REG_READ(GPIO_OUT_REG);
    uint32_t m = 0;
    const int pin[4] = { PIN_VALVE_POWER, PIN_VALVE_DIRECTION, PIN_PUMP_POWER, PIN_AC_POWER };
    for (int i = 0; i < 4; i++)
        if (((out >> pin[i]) & 1u) == (uint32_t)RELAY_ACTIVE_LEVEL) m |= 1u << i;
    return m;
}

static void trace_sample(void *arg)
{
    (void)arg;
    uint32_t m = pins_active_mask();
    if (m == trace_last) return;
    trace_last = m;
    struct { uint32_t ms, m; } e = { now_ms(), m };
    xQueueSend(TRACE_Q, &e, 0);
}

static void trace_task(void *arg)
{
    (void)arg;
    struct { uint32_t ms, m; } e;
    while (1) {
        if (xQueueReceive(TRACE_Q, &e, portMAX_DELAY) == pdTRUE)
            printf("PINS t=%u power=%u dir=%u pump=%u ac=%u\n", (unsigned)e.ms,
                   (unsigned)(e.m & 1u), (unsigned)((e.m >> 1) & 1u),
                   (unsigned)((e.m >> 2) & 1u), (unsigned)((e.m >> 3) & 1u));
    }
}

void water_bench_start_trace(void)
{
    TRACE_Q = xQueueCreate(64, 8);
    xTaskCreate(trace_task, "pin_trace", 3072, NULL, 3, NULL);
    esp_timer_handle_t t;
    esp_timer_create_args_t a = { .callback = trace_sample, .name = "pin_trace" };
    if (esp_timer_create(&a, &t) == ESP_OK) esp_timer_start_periodic(t, 5000);
}

int water_bench_pins_json(char *buf, size_t len)
{
    uint32_t out = REG_READ(GPIO_OUT_REG);
    #define LV(p) ((unsigned)((out >> (p)) & 1u))
    return snprintf(buf, len,
        "{\"power\":%u,\"dir\":%u,\"pump\":%u,\"ac\":%u,"
        "\"active_level\":%u}",
        LV(PIN_VALVE_POWER), LV(PIN_VALVE_DIRECTION), LV(PIN_PUMP_POWER),
        LV(PIN_AC_POWER), (unsigned)RELAY_ACTIVE_LEVEL);
    #undef LV
}
#endif
