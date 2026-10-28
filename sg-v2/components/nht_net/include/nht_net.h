/**
 * @file    nht_net.h
 * @brief   WiFi STA có tự kết nối lại, dự phòng chế độ AP + captive portal,
 *          và đồng bộ giờ SNTP.
 * @version 1.0.0
 * @license MIT
 *
 * Thiết bị chạy không người trông coi thì việc mất WiFi là chuyện bình
 * thường, không phải sự cố. Thư viện này xử lý theo hướng đó:
 *
 *  - Kết nối lại có giãn cách tăng dần (1s, 2s, 4s... tối đa 60s). Không có
 *    vòng lặp chờ nào chặn luồng gọi - hàm start() trả về ngay lập tức.
 *  - Chưa có mật khẩu WiFi, hoặc thử mãi không vào được, thì tự bật SoftAP
 *    kèm captive portal để người dùng nhập lại - không phải nạp lại firmware.
 *  - Vào được mạng thì tự tắt AP; rớt mạng lâu thì bật lại.
 *
 * Việc mất mạng KHÔNG được phép ảnh hưởng tới logic điều khiển. Thư viện chỉ
 * phát sự kiện; không hàm nào ở đây chặn chờ ai.
 */

#ifndef NHT_NET_H
#define NHT_NET_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NHT_NET_IDLE = 0,
    NHT_NET_CONNECTING,
    NHT_NET_CONNECTED,
    NHT_NET_AP_MODE,     /**< đang phát SoftAP để cấu hình */
} nht_net_state_t;

typedef void (*nht_net_cb_t)(nht_net_state_t st, void *user);

typedef struct {
    const char *sta_ssid;
    const char *sta_pass;

    const char *ap_ssid;          /**< NULL = không dùng chế độ AP          */
    const char *ap_pass;          /**< NULL/"" = mạng mở                    */
    uint32_t    ap_fallback_ms;   /**< thử STA bao lâu rồi mới bật AP; 0->45s */

    const char *hostname;         /**< tên mDNS/DHCP; NULL -> "nht-device"  */
    int32_t     tz_offset_s;
    const char *sntp_server;      /**< NULL -> "pool.ntp.org"               */
} nht_net_cfg_t;

/** Trả về ngay; việc kết nối chạy nền. */
esp_err_t nht_net_start(const nht_net_cfg_t *cfg);
void      nht_net_stop(void);

nht_net_state_t nht_net_state(void);
void            nht_net_on_state(nht_net_cb_t cb, void *user);

/** Địa chỉ IP hiện tại (của STA, hoặc của AP khi đang ở chế độ cấu hình). */
bool    nht_net_ip(char *buf, size_t len);
int8_t  nht_net_rssi(void);
bool    nht_net_time_synced(void);

/** Đổi mật khẩu WiFi rồi thử lại ngay, không cần khởi động lại thiết bị. */
esp_err_t nht_net_set_sta(const char *ssid, const char *pass);

/** Quét WiFi, trả về mảng JSON [{"ssid":..,"rssi":..,"secure":bool}]. */
int nht_net_scan_json(char *buf, size_t len);

/** Bật máy chủ DNS trả mọi truy vấn về IP của AP (captive portal). */
esp_err_t nht_net_captive_dns_start(void);
void      nht_net_captive_dns_stop(void);

#ifdef __cplusplus
}
#endif
#endif /* NHT_NET_H */
