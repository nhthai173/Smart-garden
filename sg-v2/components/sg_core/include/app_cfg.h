/**
 * app_cfg.h - Toàn bộ núm chỉnh của thiết bị, lưu trên NVS.
 *
 * Nguyên tắc: KHÔNG hằng số vật lý nào được hardcode trong logic. Van bi
 * quay nhanh chậm khác nhau, điện cực cảm biến khô ẩm khác nhau, cầu phân
 * áp mỗi board sai một kiểu - tất cả phải chỉnh được tại hiện trường qua
 * web, không phải nạp lại firmware.
 *
 * Giá trị mặc định dưới đây chỉ là điểm khởi đầu hợp lý để thiết bị chạy
 * được ngay từ NVS trống.
 */
#ifndef APP_CFG_H
#define APP_CFG_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define CFG_STR_LEN   64
#define CFG_URL_LEN  128

typedef struct {
    /* --- van --- */
    uint8_t  valve_type;      /**< 0 = solenoid NC, 1 = van bi động cơ      */
    uint32_t travel_ms;       /**< van bi: thời gian quay hết hành trình    */
    uint32_t pump_delay_ms;
    uint32_t ac_auto_off_ms;
    uint32_t run_default_s;
    uint32_t run_max_s;       /**< TRẦN CỨNG cho một lượt tưới              */

    /* --- cảm biến ngập --- */
    bool     leak_analog;
    bool     leak_inverted;
    uint16_t leak_thr_on;
    uint16_t leak_thr_off;
    uint32_t leak_hold_on_ms;
    uint32_t leak_hold_off_ms;

    /* --- lưu lượng & điện áp --- */
    float    flow_pulses_per_l;
    float    volt_scale;      /**< hệ số cầu phân áp                        */
    float    volt_min;        /**< dưới ngưỡng này thì cảnh báo             */

    /* --- lịch & thời gian --- */
    int32_t  tz_offset_s;
    uint16_t catchup_window_s;

    /* --- danh tính & mạng --- */
    char     dev_name[CFG_STR_LEN];
    char     wifi_ssid[CFG_STR_LEN];
    char     wifi_pass[CFG_STR_LEN];

    /* --- firebase --- */
    char     fb_api_key[CFG_STR_LEN];
    char     fb_db_url[CFG_URL_LEN];
    char     fb_email[CFG_STR_LEN];
    char     fb_pass[CFG_STR_LEN];
    char     fb_device_id[CFG_STR_LEN];
} app_cfg_t;

/** Nạp cấu hình từ NVS, điền mặc định cho khoá thiếu. */
esp_err_t app_cfg_load(void);

/** Con trỏ tới cấu hình đang dùng. Không bao giờ NULL sau app_cfg_load(). */
app_cfg_t *app_cfg(void);

/** Ghi toàn bộ cấu hình hiện tại xuống NVS. */
esp_err_t app_cfg_save(void);

/** Đặt một khoá theo tên (dùng cho REST /config). @return false nếu tên lạ. */
bool app_cfg_set_kv(const char *key, const char *value);

/** Xuất cấu hình ra JSON. Mật khẩu bị che. */
int  app_cfg_to_json(char *buf, size_t len);

/** Xoá sạch cấu hình, quay về mặc định xuất xưởng. */
esp_err_t app_cfg_factory_reset(void);

bool app_cfg_wifi_ready(void);
bool app_cfg_firebase_ready(void);

#endif /* APP_CFG_H */
