/**
 * app_bench.c - Điểm tiêm lỗi cho kiểm thử trên board thật. Chỉ build với
 * -DSG_BENCH (env esp32c3_bench); firmware phát hành không có file này.
 *
 * Board bench chưa đấu cảm biến/relay nên mọi I/O là giả. Các route dưới đây
 * cho bộ test Python đặt giá trị cảm biến, đọc mức THẬT trên chân relay, và
 * gây sự cố (treo task, crash, rớt WiFi) để xem van có đóng như thiết kế.
 */
#include "sdkconfig.h"

#if defined(SG_BENCH)

#include "app_http.h"
#include "app_cfg.h"
#include "app_water.h"

#include "nht_webkit.h"
#include "nht_net.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "bench";

static int qint(httpd_req_t *r, const char *key, int def)
{
    char v[16];
    return nht_web_query(r, key, v, sizeof(v)) > 0 ? atoi(v) : def;
}

static esp_err_t h_pins(httpd_req_t *r)
{
    char buf[128];
    water_bench_pins_json(buf, sizeof(buf));
    return nht_web_json(r, buf);
}

static esp_err_t h_leak(httpd_req_t *r)
{
    water_bench_set_leak_raw((uint16_t)qint(r, "raw", 0));
    return nht_web_json(r, "{\"ok\":true}");
}

static esp_err_t h_volt(httpd_req_t *r)
{
    water_bench_set_volt_raw((uint16_t)qint(r, "raw", 0));
    return nht_web_json(r, "{\"ok\":true}");
}

static esp_err_t h_leak_fail(httpd_req_t *r)
{
    water_bench_fail_leak_adc(qint(r, "on", 1) != 0);
    return nht_web_json(r, "{\"ok\":true}");
}

static esp_err_t h_stall(httpd_req_t *r)
{
    water_bench_stall_ctl((uint32_t)qint(r, "ms", 0));
    return nht_web_json(r, "{\"ok\":true}");
}

static void do_crash(void *arg)
{
    (void)arg;
    ESP_LOGW(TAG, "BENCH: cố tình crash (abort)");
    abort();
}

static esp_err_t h_crash(httpd_req_t *r)
{
    nht_web_json(r, "{\"ok\":true}");
    esp_timer_handle_t t;
    esp_timer_create_args_t a = { .callback = do_crash, .name = "crash" };
    if (esp_timer_create(&a, &t) == ESP_OK) esp_timer_start_once(t, 200 * 1000);
    return ESP_OK;
}

/* --- rớt WiFi giả: đổi sang một SSID không tồn tại rồi trả lại sau `ms` --- */
static void wifi_restore(void *arg)
{
    (void)arg;
    app_cfg_t *c = app_cfg();
    ESP_LOGW(TAG, "BENCH: trả WiFi về '%s'", c->wifi_ssid);
    nht_net_set_sta(c->wifi_ssid, c->wifi_pass);
}

static esp_err_t h_wifi_drop(httpd_req_t *r)
{
    int ms = qint(r, "ms", 30000);
    nht_web_json(r, "{\"ok\":true}");

    ESP_LOGW(TAG, "BENCH: rớt WiFi %d ms", ms);
    nht_net_set_sta("sg-bench-no-such-ap", "00000000");

    esp_timer_handle_t t;
    esp_timer_create_args_t a = { .callback = wifi_restore, .name = "wifi_back" };
    if (esp_timer_create(&a, &t) == ESP_OK)
        esp_timer_start_once(t, (uint64_t)ms * 1000ULL);
    return ESP_OK;
}

void app_bench_register(void)
{
    nht_web_route("/api/bench/pins",      HTTP_GET,  h_pins,      NULL);
    nht_web_route("/api/bench/leak",      HTTP_POST, h_leak,      NULL);
    nht_web_route("/api/bench/volt",      HTTP_POST, h_volt,      NULL);
    nht_web_route("/api/bench/leak_fail", HTTP_POST, h_leak_fail, NULL);
    nht_web_route("/api/bench/stall",     HTTP_POST, h_stall,     NULL);
    nht_web_route("/api/bench/crash",     HTTP_POST, h_crash,     NULL);
    nht_web_route("/api/bench/wifi_drop", HTTP_POST, h_wifi_drop, NULL);
    ESP_LOGW(TAG, "BẢN BENCH: route tiêm lỗi đang mở - KHÔNG phát hành bản này");
}

#endif /* SG_BENCH */
