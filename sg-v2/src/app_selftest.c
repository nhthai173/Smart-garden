/**
 * @file    app_selftest.c
 * @brief   Integration tests that RUN ON THE TARGET. Built only with -DSG_SIM.
 *
 * The PC tests (pio test -e native) already cover the pure logic. This file covers
 * what only exists on the chip: real NVS, real esp_timer, real GPIO, and real
 * FreeRTOS tasks interleaving.
 *
 * Runs in QEMU. QEMU has no radio (esp_phy_enable would assert), so the network
 * layer is switched off entirely in this mode - which is itself a good test: it
 * proves that watering works with NO network at all.
 */

#if defined(SG_SIM)

#include "app_selftest.h"
#include "app_cfg.h"
#include "app_water.h"
#include "board_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nht_adc.h"
#include "nht_sched.h"

/* Whether the settings have been pushed into the running structures yet. */
#define LEAK_HOLD_APPLIED() (app_cfg()->leak_hold_off_ms == 3000)

#include <string.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "selftest";

static int passed, failed;

#define CHECK(cond, fmt, ...)                                        \
    do {                                                             \
        if (cond) { passed++; ESP_LOGI(TAG, "  ĐẠT  " fmt, ##__VA_ARGS__); } \
        else      { failed++; ESP_LOGE(TAG, "  HỎNG " fmt, ##__VA_ARGS__); } \
    } while (0)

static void wait_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

static const char *vstate(void)
{
    water_status_t s;
    water_get_status(&s);
    return nht_valve_state_str(s.valve);
}

static water_status_t snap(void)
{
    water_status_t s;
    water_get_status(&s);
    return s;
}

/* A ball valve needs `travel_ms` to turn end to end. */
static void wait_travel(void) { wait_ms(app_cfg()->travel_ms + 600); }

static void t_boot_state(void)
{
    ESP_LOGI(TAG, "[1] Trạng thái khi khởi động");
    wait_travel();
    CHECK(!strcmp(vstate(), "closed"), "van ĐÓNG sau khi khởi động (%s)", vstate());
    CHECK(!strcmp(app_cfg()->dev_name, "Smart Garden"),
          "giá trị mặc định chuỗi sống sót khi NVS trống (dev_name='%s')",
          app_cfg()->dev_name);
    CHECK(!snap().leak, "không báo ngập khi cảm biến khô");
}

static void t_open_close(void)
{
    ESP_LOGI(TAG, "[2] Mở và đóng van");

    nht_valve_err_t e = water_open(20, 100, WSRC_LOCAL_API);
    CHECK(e == NHT_VALVE_OK, "lệnh mở được nhận (mã %d)", e);
    wait_ms(200);
    CHECK(!strcmp(vstate(), "opening"), "van đang quay để mở (%s)", vstate());

    wait_travel();
    CHECK(!strcmp(vstate(), "open"), "van đã mở hẳn (%s)", vstate());

    uint32_t left = snap().remaining_s;
    CHECK(left > 5 && left <= 20, "còn %us trong khoảng hợp lý", (unsigned)left);

    water_close(WSRC_LOCAL_API);
    wait_travel();
    CHECK(!strcmp(vstate(), "closed"), "van đã đóng (%s)", vstate());
}

static void t_duration_survives_duplicate(void)
{
    ESP_LOGI(TAG, "[3] Lệnh trùng KHÔNG làm mất thời lượng (lỗi của bản cũ)");

    water_open(30, 100, WSRC_LOCAL_API);
    wait_travel();
    uint32_t a = snap().remaining_s;

    /* The identical command echoing back, like a sync event from the cloud. */
    water_open(30, 100, WSRC_CLOUD);
    water_open(30, 100, WSRC_CLOUD);
    wait_ms(300);
    uint32_t b = snap().remaining_s;

    CHECK(b <= a && b + 3 >= a, "thời lượng giữ nguyên (%us -> %us)",
          (unsigned)a, (unsigned)b);

    /* Past the 60 s default and still open? No - here the 30 s duration is SHORTER
       than the default, so it only has to be shown to close on time and not stretch to 60 s. */
    wait_ms(31000);
    CHECK(strcmp(vstate(), "open") != 0, "tự đóng khi hết 30s (%s)", vstate());
    wait_travel();
}

static void t_max_run_ceiling(void)
{
    ESP_LOGI(TAG, "[4] Trần thời gian tưới là chặn cứng");

    water_open(99999, 100, WSRC_CLOUD);   /* ask for ~27 hours */
    wait_travel();
    uint32_t left = snap().remaining_s;
    CHECK(left <= app_cfg()->run_max_s,
          "bị kẹp xuống %us (trần %us)",
          (unsigned)left, (unsigned)app_cfg()->run_max_s);

    water_close(WSRC_LOCAL_API);
    wait_travel();
}

static void t_leak_interlock(void)
{
    ESP_LOGI(TAG, "[5] Ngập nước khoá van");

    water_open(60, 100, WSRC_LOCAL_API);
    wait_travel();
    CHECK(!strcmp(vstate(), "open"), "đang tưới trước khi mô phỏng ngập");

    /* Push the sensor value above the leak-entry threshold. */
    nht_adc_fake_set(PIN_LEAK_ADC, app_cfg()->leak_thr_on + 300);

    /* The entry delay is leak_hold_on_ms. */
    wait_ms(app_cfg()->leak_hold_on_ms + 1500);
    CHECK(snap().leak, "đã công nhận có ngập");

    wait_travel();
    CHECK(strcmp(vstate(), "open") != 0, "van bị đóng do ngập (%s)", vstate());

    nht_valve_err_t e = water_open(60, 100, WSRC_LOCAL_API);
    CHECK(e == NHT_VALVE_ERR_INHIBIT, "lệnh mở bị từ chối khi đang ngập (mã %d)", e);

    /* Let it dry again. The exit delay is leak_hold_off_ms - shortened here so the
       test finishes; the real 10 minutes is what kills false alarms. */
    ESP_LOGI(TAG, "  (rút ngắn trễ thoát ngập để test chạy kịp)");
    app_cfg()->leak_hold_off_ms = 3000;
    water_apply_cfg();
    nht_adc_fake_set(PIN_LEAK_ADC, 100);
    wait_ms(5000);
    CHECK(!snap().leak, "đã hết ngập");
    CHECK(LEAK_HOLD_APPLIED(), "cấu hình mới có hiệu lực ngay, không cần reboot");

    e = water_open(10, 100, WSRC_LOCAL_API);
    CHECK(e == NHT_VALVE_OK, "mở lại được sau khi hết ngập (mã %d)", e);
    water_close(WSRC_LOCAL_API);
    wait_travel();
}

static void t_voltage_failsafe(void)
{
    ESP_LOGI(TAG, "[6b] Nguồn sụt: đóng van ngay, nhiễu thoáng qua thì bỏ qua");

    water_open(60, 100, WSRC_LOCAL_API);
    wait_travel();
    CHECK(!strcmp(vstate(), "open"), "đang tưới");

    /* A passing dip (< 3 consecutive samples): must NOT close the valve. */
    nht_adc_fake_set(PIN_VOLTAGE_ADC, 300);
    wait_ms(1500);
    nht_adc_fake_set(PIN_VOLTAGE_ADC, 1500);
    wait_ms(2500);
    CHECK(!snap().voltage_low, "cú sụt thoáng qua không bị coi là sự cố");
    CHECK(!strcmp(vstate(), "open"), "van vẫn mở sau nhiễu thoáng qua");

    /* A sustained dip: must close. */
    nht_adc_fake_set(PIN_VOLTAGE_ADC, 300);
    wait_ms(4500);
    CHECK(snap().faults & WF_VOLTAGE, "nguồn thấp kéo dài -> sự cố");
    CHECK(strcmp(vstate(), "open") != 0, "van đã bị lệnh đóng (%s)", vstate());
    wait_travel();
    CHECK(!strcmp(vstate(), "closed"), "van đã đóng hẳn (%s)", vstate());
    nht_valve_err_t e = water_open(10, 100, WSRC_LOCAL_API);
    CHECK(e == NHT_VALVE_ERR_INHIBIT, "không mở được khi nguồn yếu (mã %d)", e);

    /* The supply recovers and stays stable: the lock is released. */
    nht_adc_fake_set(PIN_VOLTAGE_ADC, 1500);
    wait_ms(7000);
    CHECK(!(snap().faults & WF_VOLTAGE), "nguồn ổn định lại -> gỡ sự cố");
    e = water_open(10, 100, WSRC_LOCAL_API);
    CHECK(e == NHT_VALVE_OK, "mở lại được (mã %d)", e);
    water_close(WSRC_LOCAL_API);
    wait_travel();
}

static void t_leak_flapping(void)
{
    ESP_LOGI(TAG, "[6] Dao động quanh ngưỡng KHÔNG sinh báo ảo (lỗi của bản cũ)");

    app_cfg()->leak_hold_off_ms = 3000;
    water_apply_cfg();
    nht_adc_fake_set(PIN_LEAK_ADC, 100);
    wait_ms(4000);

    water_status_t before = snap();
    CHECK(!before.leak, "bắt đầu ở trạng thái khô");

    /* A damp sensor: the value jumps around the dead band between the two thresholds
       for 12 seconds. No sample exceeds thr_on, so there must be no leak alarm. */
    uint16_t lo = app_cfg()->leak_thr_off + 50;
    uint16_t hi = app_cfg()->leak_thr_on - 50;
    for (int i = 0; i < 24; i++) {
        nht_adc_fake_set(PIN_LEAK_ADC, (i % 2) ? hi : lo);
        wait_ms(500);
    }
    CHECK(!snap().leak, "vẫn không báo ngập sau 12s dao động quanh ngưỡng");

    nht_adc_fake_set(PIN_LEAK_ADC, 100);
    wait_ms(4000);
}

static void t_schedule_persist(void)
{
    ESP_LOGI(TAG, "[7] Lịch tưới lưu xuống NVS");

    water_sched_clear();
    uint8_t id = 0;
    bool ok = water_sched_add(6, 30, NHT_SCHED_REPEAT_DAILY, 900, 100, true, &id);
    CHECK(ok && id > 0, "thêm được lịch (id=%u)", id);

    char buf[1024];
    water_sched_json(buf, sizeof(buf));
    CHECK(strstr(buf, "\"hour\":6") != NULL, "lịch có trong danh sách");

    ok = water_sched_update(id, 18, 0, NHT_SCHED_REPEAT_DAILY, 1200, 50, true);
    CHECK(ok, "sửa được lịch");

    water_sched_json(buf, sizeof(buf));
    CHECK(strstr(buf, "\"hour\":18") != NULL, "sửa đã có hiệu lực thật");
    CHECK(strstr(buf, "\"duration_s\":1200") != NULL, "thời lượng mới đã lưu");

    CHECK(water_sched_remove(id), "xoá được lịch");
    water_sched_json(buf, sizeof(buf));
    CHECK(!strcmp(buf, "[]"), "danh sách rỗng sau khi xoá (%s)", buf);
}

static void t_schedule_fires(void)
{
    ESP_LOGI(TAG, "[8] Lịch tự kích khi tới giờ");

    /* Set the system clock to 05:59:55 local time on a specific day, then
       schedule 06:00 - only a 5-second wait. */
    int32_t tz = app_cfg()->tz_offset_s;
    time_t target = (time_t)20691 * 86400 + 6 * 3600 - 5 - tz;
    struct timeval tv = { .tv_sec = target, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    water_sched_clear();
    uint8_t id;
    water_sched_add(6, 0, NHT_SCHED_REPEAT_DAILY, 15, 100, true, &id);

    ESP_LOGI(TAG, "  chờ tới 06:00…");
    for (int i = 0; i < 20 && strcmp(vstate(), "closed") == 0; i++)
        wait_ms(1000);

    water_status_t s = snap();
    CHECK(s.valve != NHT_VALVE_CLOSED, "lịch đã kích van (%s)",
          nht_valve_state_str(s.valve));
    CHECK(s.last_src == WSRC_SCHEDULE, "nguồn lệnh là lịch (%s)",
          water_src_str(s.last_src));

    water_close(WSRC_LOCAL_API);
    wait_travel();
    water_sched_clear();
}

static void t_catchup(void)
{
    ESP_LOGI(TAG, "[9] Cửa sổ bù giờ: tick trễ vẫn tưới (lỗi của bản cũ)");

    /* Jump straight to 06:03 - the scheduled time is already 3 minutes PAST. The old
       exact-minute matching would have dropped today's run altogether. */
    int32_t tz = app_cfg()->tz_offset_s;
    time_t target = (time_t)20692 * 86400 + 6 * 3600 + 180 - tz;
    struct timeval tv = { .tv_sec = target, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    water_sched_clear();
    uint8_t id;
    water_sched_add(6, 0, NHT_SCHED_REPEAT_DAILY, 15, 100, true, &id);

    for (int i = 0; i < 6 && strcmp(vstate(), "closed") == 0; i++)
        wait_ms(1000);

    CHECK(strcmp(vstate(), "closed") != 0,
          "lịch trễ 3 phút vẫn chạy (%s)", vstate());

    water_close(WSRC_LOCAL_API);
    wait_travel();
    water_sched_clear();
}

static void t_config_roundtrip(void)
{
    ESP_LOGI(TAG, "[10] Cấu hình ghi/đọc lại từ NVS");

    uint32_t orig = app_cfg()->travel_ms;
    CHECK(app_cfg_set_kv("travel_ms", "5500"), "đặt được khoá travel_ms");
    CHECK(app_cfg()->travel_ms == 5500, "giá trị đã vào bộ nhớ");
    CHECK(app_cfg_save() == ESP_OK, "ghi xuống NVS");

    CHECK(!app_cfg_set_kv("khoa_khong_ton_tai", "1"), "khoá lạ bị từ chối");

    char json[2048];
    int n = app_cfg_to_json(json, sizeof(json));
    CHECK(n > 0 && n < (int)sizeof(json), "xuất JSON được (%d byte)", n);
    CHECK(strstr(json, "\"fb_pass_set\"") != NULL, "mật khẩu không lộ ra JSON");

    app_cfg_set_kv("travel_ms", "8000");
    app_cfg_save();
    (void)orig;
}

static void selftest_task(void *arg)
{
    (void)arg;
    wait_ms(1500);

    ESP_LOGI(TAG, "════════ KIỂM THỬ TRÊN TARGET ════════");
    nht_adc_fake_set(PIN_LEAK_ADC, 100);
    nht_adc_fake_set(PIN_VOLTAGE_ADC, 1500);

    t_boot_state();
    t_open_close();
    t_duration_survives_duplicate();
    t_max_run_ceiling();
    t_leak_interlock();
    t_leak_flapping();
    t_voltage_failsafe();
    t_schedule_persist();
    t_schedule_fires();
    t_catchup();
    t_config_roundtrip();

    ESP_LOGI(TAG, "══════════════════════════════════════");
    ESP_LOGI(TAG, "KẾT QUẢ: %d đạt, %d hỏng", passed, failed);
    ESP_LOGI(TAG, "%s", failed == 0 ? "SELFTEST: TAT CA DAT"
                                    : "SELFTEST: CO LOI");
    ESP_LOGI(TAG, "══════════════════════════════════════");

    vTaskDelete(NULL);
}

void app_selftest_start(void)
{
    xTaskCreate(selftest_task, "selftest", 5120, NULL, 3, NULL);
}

#endif /* SG_SIM */
