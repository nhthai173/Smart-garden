#include "nht_net.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "nht_net";

#define MAX_STR 64

static struct {
    char sta_ssid[MAX_STR], sta_pass[MAX_STR];
    char ap_ssid[MAX_STR],  ap_pass[MAX_STR];
    char hostname[MAX_STR];
    char sntp_server[MAX_STR];
    uint32_t ap_fallback_ms;
    int32_t  tz_offset_s;
} CFG;

static nht_net_state_t  STATE = NHT_NET_IDLE;
static esp_timer_handle_t RETRY_T;

/* Hẹn giờ nối lại bằng timer, KHÔNG vTaskDelay trong handler: handler chạy
   trên task event loop mặc định của hệ thống, ngủ ở đó tối đa 60 s là chặn
   luôn mọi sự kiện khác (IP, SNTP, chương trình khác dùng chung loop). */
static void retry_cb(void *arg)
{
    (void)arg;
    esp_wifi_connect();
}

static void schedule_retry(uint32_t ms)
{
    if (!RETRY_T) {
        esp_timer_create_args_t a = { .callback = retry_cb, .name = "wifi_retry" };
        if (esp_timer_create(&a, &RETRY_T) != ESP_OK) { esp_wifi_connect(); return; }
    }
    esp_timer_stop(RETRY_T);
    esp_timer_start_once(RETRY_T, (uint64_t)ms * 1000ULL);
}
static nht_net_cb_t     CB;
static void            *CB_USER;
static esp_netif_t     *STA_IF, *AP_IF;
static bool             started, ap_on, time_ok, sntp_started;
static uint32_t         retry_n;
static int64_t          connecting_since_us;
static TaskHandle_t     dns_task_handle;
static volatile bool    dns_run;

static void set_state(nht_net_state_t s)
{
    if (STATE == s) return;
    STATE = s;
    if (CB) CB(s, CB_USER);
}

/* Giãn cách tăng dần: 1,2,4,8,...,60 giây. Mạng chập chờn thì không nên
   quay vòng thử lại liên tục - vừa tốn điện vừa làm nhiễu chính nó. */
static uint32_t backoff_ms(void)
{
    uint32_t ms = 1000u << (retry_n > 6 ? 6 : retry_n);
    return ms > 60000u ? 60000u : ms;
}

static void start_ap(void);
static void stop_ap(void);

static void on_sntp_sync(struct timeval *tv)
{
    (void)tv;
    time_ok = true;
    ESP_LOGI(TAG, "đã đồng bộ giờ qua SNTP");
}

static void start_sntp(void)
{
    if (sntp_started) return;
    esp_sntp_config_t c = ESP_NETIF_SNTP_DEFAULT_CONFIG(CFG.sntp_server);
    c.sync_cb = on_sntp_sync;
    c.start   = true;
    c.server_from_dhcp = false;
    if (esp_netif_sntp_init(&c) == ESP_OK) sntp_started = true;
}

/* Mạng mesh/nhiều AP cùng SSID: mặc định IDF nối vào AP đầu tiên tìm thấy
   (FAST_SCAN), thường là một nút xa và yếu dù có nút gần hơn nhiều. Quét hết
   các kênh và chọn theo cường độ. */
static void fill_sta(wifi_config_t *sta)
{
    strncpy((char *)sta->sta.ssid, CFG.sta_ssid, sizeof(sta->sta.ssid) - 1);
    strncpy((char *)sta->sta.password, CFG.sta_pass, sizeof(sta->sta.password) - 1);
    sta->sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sta->sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
}

#define ROAM_RSSI_DBM      (-75)       /* yếu hơn mức này thì thử tìm AP tốt hơn */
#define ROAM_MIN_GAP_US    (300LL * 1000000)
static int64_t last_roam_us;

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;

    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_CONNECTED:
            /* Tụt dưới ngưỡng thì WIFI_EVENT_STA_BSS_RSSI_LOW bắn ra. */
            esp_wifi_set_rssi_threshold(ROAM_RSSI_DBM);
            break;

        case WIFI_EVENT_STA_BSS_RSSI_LOW: {
            int64_t now = esp_timer_get_time();
            if (now - last_roam_us > ROAM_MIN_GAP_US) {
                last_roam_us = now;
                ESP_LOGW(TAG, "tín hiệu yếu (RSSI < %d), nối lại để chọn AP mạnh nhất",
                         ROAM_RSSI_DBM);
                esp_wifi_disconnect();    /* handler sẽ nối lại, quét lại hết kênh */
            }
            break;
        }

        case WIFI_EVENT_STA_START:
            connecting_since_us = esp_timer_get_time();
            esp_wifi_connect();
            set_state(NHT_NET_CONNECTING);
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            if (STATE == NHT_NET_CONNECTED) {
                ESP_LOGW(TAG, "mất kết nối WiFi");
                connecting_since_us = esp_timer_get_time();
                retry_n = 0;
            }
            set_state(ap_on ? NHT_NET_AP_MODE : NHT_NET_CONNECTING);

            uint32_t wait = backoff_ms();
            if (retry_n < 32) retry_n++;

            /* Thử mãi không được thì bật AP để người dùng nhập lại mật khẩu,
               nhưng VẪN tiếp tục thử STA - có thể router chỉ đang khởi động. */
            int64_t elapsed = (esp_timer_get_time() - connecting_since_us) / 1000;
            if (!ap_on && CFG.ap_ssid[0] &&
                elapsed > (int64_t)CFG.ap_fallback_ms) {
                ESP_LOGW(TAG, "không vào được WiFi sau %llds, bật chế độ cấu hình",
                         (long long)(elapsed / 1000));
                start_ap();
            }

            schedule_retry(wait);
            break;
        }
        default: break;
        }
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "đã vào WiFi, IP " IPSTR, IP2STR(&e->ip_info.ip));
        retry_n = 0;
        set_state(NHT_NET_CONNECTED);
        if (ap_on) stop_ap();
        start_sntp();
    }
}

/* ------------------------------------------------------------------ */
/* Captive portal DNS: trả mọi truy vấn về IP của AP                   */
/* ------------------------------------------------------------------ */

static void dns_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { dns_task_handle = NULL; vTaskDelete(NULL); return; }

    struct sockaddr_in me = {
        .sin_family = AF_INET,
        .sin_port   = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&me, sizeof(me)) < 0) {
        close(sock);
        dns_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    struct timeval tv = { .tv_sec = 1 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    esp_netif_ip_info_t ip;
    esp_netif_get_ip_info(AP_IF, &ip);

    uint8_t buf[256];
    ESP_LOGI(TAG, "captive DNS đang chạy");

    while (dns_run) {
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf), 0,
                         (struct sockaddr *)&from, &flen);
        if (n < 12) continue;

        /* Chỉ trả lời truy vấn A đơn giản: cờ = phản hồi, 1 câu hỏi 1 đáp án,
           đáp án trỏ về IP của chính AP. Đủ để điện thoại mở trang cấu hình. */
        buf[2] = 0x81; buf[3] = 0x80;      /* response, no error */
        buf[6] = 0x00; buf[7] = 0x01;      /* answer count = 1   */
        buf[8] = 0x00; buf[9] = 0x00;
        buf[10] = 0x00; buf[11] = 0x00;

        if (n + 16 > (int)sizeof(buf)) continue;
        uint8_t *p = buf + n;
        *p++ = 0xC0; *p++ = 0x0C;          /* con trỏ về tên trong câu hỏi */
        *p++ = 0x00; *p++ = 0x01;          /* type A    */
        *p++ = 0x00; *p++ = 0x01;          /* class IN  */
        *p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x0A;  /* TTL 10s */
        *p++ = 0x00; *p++ = 0x04;          /* độ dài dữ liệu */
        memcpy(p, &ip.ip.addr, 4);
        p += 4;

        sendto(sock, buf, p - buf, 0, (struct sockaddr *)&from, flen);
    }

    close(sock);
    ESP_LOGI(TAG, "captive DNS dừng");
    dns_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t nht_net_captive_dns_start(void)
{
    if (dns_task_handle) return ESP_OK;
    dns_run = true;
    return xTaskCreate(dns_task, "captive_dns", 3072, NULL, 4, &dns_task_handle)
               == pdPASS ? ESP_OK : ESP_FAIL;
}

void nht_net_captive_dns_stop(void) { dns_run = false; }

/* ------------------------------------------------------------------ */

static void start_ap(void)
{
    if (ap_on) return;

    wifi_config_t ap = {0};
    strncpy((char *)ap.ap.ssid, CFG.ap_ssid, sizeof(ap.ap.ssid) - 1);
    ap.ap.ssid_len       = strlen(CFG.ap_ssid);
    ap.ap.max_connection = 4;
    ap.ap.channel        = 1;

    if (CFG.ap_pass[0] && strlen(CFG.ap_pass) >= 8) {
        strncpy((char *)ap.ap.password, CFG.ap_pass, sizeof(ap.ap.password) - 1);
        ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        ap.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));

    ap_on = true;
    set_state(NHT_NET_AP_MODE);
    nht_net_captive_dns_start();
    ESP_LOGI(TAG, "SoftAP '%s' đang phát để cấu hình", CFG.ap_ssid);
}

static void stop_ap(void)
{
    if (!ap_on) return;
    nht_net_captive_dns_stop();
    esp_wifi_set_mode(WIFI_MODE_STA);
    ap_on = false;
    ESP_LOGI(TAG, "tắt SoftAP");
}

static void cpy(char *dst, const char *src, const char *dflt)
{
    strncpy(dst, src && src[0] ? src : (dflt ? dflt : ""), MAX_STR - 1);
    dst[MAX_STR - 1] = '\0';
}

esp_err_t nht_net_start(const nht_net_cfg_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    if (started) return ESP_OK;

    cpy(CFG.sta_ssid, cfg->sta_ssid, NULL);
    cpy(CFG.sta_pass, cfg->sta_pass, NULL);
    cpy(CFG.ap_ssid,  cfg->ap_ssid,  NULL);
    cpy(CFG.ap_pass,  cfg->ap_pass,  NULL);
    cpy(CFG.hostname, cfg->hostname, "nht-device");
    cpy(CFG.sntp_server, cfg->sntp_server, "pool.ntp.org");
    CFG.ap_fallback_ms = cfg->ap_fallback_ms ? cfg->ap_fallback_ms : 45000;
    CFG.tz_offset_s    = cfg->tz_offset_s;

    /* Đặt múi giờ cho localtime(); giờ lưu trong hệ vẫn là UTC. */
    char tz[32];
    int off_h = -(int)(CFG.tz_offset_s / 3600);   /* POSIX TZ ngược dấu */
    snprintf(tz, sizeof(tz), "UTC%+d", off_h);
    setenv("TZ", tz, 1);
    tzset();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    STA_IF = esp_netif_create_default_wifi_sta();
    AP_IF  = esp_netif_create_default_wifi_ap();
    esp_netif_set_hostname(STA_IF, CFG.hostname);

    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&ic));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    started = true;

    if (CFG.sta_ssid[0]) {
        wifi_config_t sta = {0};
        fill_sta(&sta);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
        ESP_ERROR_CHECK(esp_wifi_start());
    } else {
        /* Chưa cấu hình gì: vào thẳng chế độ cấu hình. */
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_start());
        start_ap();
    }
    return ESP_OK;
}

void nht_net_stop(void)
{
    if (!started) return;
    nht_net_captive_dns_stop();
    esp_wifi_stop();
    started = false;
    set_state(NHT_NET_IDLE);
}

esp_err_t nht_net_set_sta(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0]) return ESP_ERR_INVALID_ARG;
    cpy(CFG.sta_ssid, ssid, NULL);
    cpy(CFG.sta_pass, pass, NULL);

    wifi_config_t sta = {0};
    fill_sta(&sta);

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta);
    if (err != ESP_OK) return err;

    retry_n = 0;
    connecting_since_us = esp_timer_get_time();
    esp_wifi_disconnect();
    return esp_wifi_connect();
}

nht_net_state_t nht_net_state(void) { return STATE; }

void nht_net_on_state(nht_net_cb_t cb, void *user)
{
    CB = cb;
    CB_USER = user;
}

bool nht_net_ip(char *buf, size_t len)
{
    esp_netif_ip_info_t ip;
    esp_netif_t *nif = (STATE == NHT_NET_CONNECTED) ? STA_IF : AP_IF;
    if (!nif || esp_netif_get_ip_info(nif, &ip) != ESP_OK) return false;
    snprintf(buf, len, IPSTR, IP2STR(&ip.ip));
    return ip.ip.addr != 0;
}

int8_t nht_net_rssi(void)
{
    wifi_ap_record_t r;
    if (esp_wifi_sta_get_ap_info(&r) == ESP_OK) return r.rssi;
    return 0;
}

bool nht_net_time_synced(void)
{
    if (time_ok) return true;
    return time(NULL) > 1700000000;
}

int nht_net_scan_json(char *buf, size_t len)
{
    wifi_scan_config_t sc = { .show_hidden = false };
    if (esp_wifi_scan_start(&sc, true) != ESP_OK)
        return snprintf(buf, len, "[]");

    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n > 20) n = 20;

    wifi_ap_record_t *recs = calloc(n, sizeof(*recs));
    if (!recs) return snprintf(buf, len, "[]");
    esp_wifi_scan_get_ap_records(&n, recs);

    int w = snprintf(buf, len, "[");
    for (uint16_t i = 0; i < n && w < (int)len; i++) {
        w += snprintf(buf + w, len - w, "%s{\"ssid\":\"%s\",\"rssi\":%d,"
                      "\"secure\":%s}",
                      i ? "," : "", (char *)recs[i].ssid, recs[i].rssi,
                      recs[i].authmode == WIFI_AUTH_OPEN ? "false" : "true");
    }
    w += snprintf(buf + w, len > (size_t)w ? len - w : 0, "]");
    free(recs);
    return w;
}
