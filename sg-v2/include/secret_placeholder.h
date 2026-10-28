/**
 * secret.h - Giá trị mặc định lúc phát triển. Chép thành secret.h rồi điền.
 *
 * app_cfg.c dò file này bằng __has_include. Có thì các khoá dưới đây được
 * dùng làm mặc định khi NVS còn trống; người dùng cuối vẫn nhập lại được
 * qua captive portal, và giá trị trong NVS luôn thắng.
 */
#ifndef SG_SECRET_H
#define SG_SECRET_H

/* WiFi thật, dùng cho bản nạp lên board. */
#define SECRET_WIFI_SSID      ""
#define SECRET_WIFI_PASS      ""

/* Trình mô phỏng cấp sẵn một mạng mở tên Wokwi-GUEST ra được internet.
   Chỉ đè lên khi build bản mô phỏng - KHÔNG để bản nạp lên board thật đi
   tìm một mạng không tồn tại. */
#if defined(SG_WOKWI)
#undef  SECRET_WIFI_SSID
#undef  SECRET_WIFI_PASS
#define SECRET_WIFI_SSID      "Wokwi-GUEST"
#define SECRET_WIFI_PASS      ""
#endif

#define SECRET_FB_API_KEY     ""
#define SECRET_FB_DB_URL      ""
#define SECRET_FB_EMAIL       ""
#define SECRET_FB_PASS        ""

/* Nhánh riêng để thử nghiệm - KHÔNG đụng vào dữ liệu của thiết bị v1
   đang chạy thật. */
#define SECRET_FB_DEVICE_ID   "sg-v2-dev"

#endif /* SG_SECRET_H */
