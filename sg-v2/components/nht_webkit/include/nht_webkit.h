/**
 * @file    nht_webkit.h
 * @brief   Máy chủ HTTP + WebSocket + OTA qua LAN cho ESP-IDF.
 * @version 1.0.0
 * @license MIT
 *
 * Bọc `esp_http_server` lại cùng những thứ mà thiết bị IoT nào cũng phải
 * viết lại: phục vụ trang nhúng dạng gzip, đáp JSON, đọc tham số truy vấn
 * và thân request, phát WebSocket tới mọi client, và một endpoint nạp
 * firmware qua LAN.
 *
 * VỀ OTA: firmware được nạp thẳng qua HTTP trong mạng nội bộ, ghi trực tiếp
 * vào partition OTA theo từng khối. Không có bước trung gian nào - không
 * base64, không tải qua cloud, không nạp cả ảnh firmware vào RAM. Cách tải
 * firmware về qua dịch vụ đám mây rồi giữ trong bộ nhớ là một nguồn gây
 * treo máy quen thuộc trên chip có heap nhỏ.
 *
 * Sau khi ghi xong, ảnh mới được đánh dấu "chờ kiểm chứng". Ứng dụng phải
 * gọi esp_ota_mark_app_valid_cancel_rollback() khi đã chạy ổn; nếu không,
 * bootloader tự quay về bản cũ ở lần khởi động sau.
 */

#ifndef NHT_WEBKIT_H
#define NHT_WEBKIT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t port;              /**< 0 -> 80                                */
    size_t   stack_size;        /**< 0 -> 8192                              */
    uint16_t max_handlers;      /**< 0 -> 24                                */
    bool     enable_ws;
    const char *ws_path;        /**< NULL -> "/ws"                          */
    bool     enable_ota;
    const char *ota_path;       /**< NULL -> "/update"                      */
    bool     captive;           /**< chuyển hướng mọi URL lạ về "/"         */
    void (*on_ota_progress)(size_t done, size_t total, void *user);
    void (*on_ota_done)(bool ok, void *user);
    void  *user;
} nht_web_cfg_t;

esp_err_t      nht_web_start(const nht_web_cfg_t *cfg);
void           nht_web_stop(void);
httpd_handle_t nht_web_handle(void);

/** Đăng ký một route. Gọi sau nht_web_start(). */
esp_err_t nht_web_route(const char *uri, httpd_method_t method,
                        esp_err_t (*handler)(httpd_req_t *), void *ctx);

/* --- tiện ích đáp --- */
esp_err_t nht_web_json(httpd_req_t *r, const char *json);
esp_err_t nht_web_text(httpd_req_t *r, const char *text);
esp_err_t nht_web_error(httpd_req_t *r, int code, const char *message);

/** Phục vụ một mảng byte đã gzip (trang nhúng trong flash). */
esp_err_t nht_web_gz(httpd_req_t *r, const uint8_t *gz, size_t len,
                     const char *content_type);

/* --- tiện ích đọc --- */

/** @return độ dài giá trị, hoặc -1 nếu không có tham số đó. */
int nht_web_query(httpd_req_t *r, const char *key, char *out, size_t out_len);

/**
 * true nếu chuỗi JSON lồng không quá `max_depth` tầng ([ và { ngoài chuỗi).
 *
 * BẮT BUỘC gọi trước cJSON_Parse với dữ liệu không tin cậy: cJSON đệ quy theo
 * độ sâu (giới hạn mặc định 1000 tầng), một thân request "[[[[...": vài trăm
 * ký tự là đủ làm tràn stack của task HTTP và reset thiết bị.
 */
bool nht_json_depth_ok(const char *s, unsigned max_depth);

/** Đọc toàn bộ thân request (đã thêm '\0'). @return số byte, <0 nếu lỗi. */
int nht_web_body(httpd_req_t *r, char *buf, size_t buf_len);

/* --- WebSocket --- */

/** Gửi một khung text tới mọi client đang mở. An toàn khi chưa có ai. */
esp_err_t nht_web_ws_broadcast(const char *text);
int       nht_web_ws_client_count(void);

/** Nhận khung text từ client. Đặt trước nht_web_start(). */
typedef void (*nht_web_ws_cb_t)(httpd_req_t *r, const char *text, void *user);
void nht_web_ws_on_message(nht_web_ws_cb_t cb, void *user);

#ifdef __cplusplus
}
#endif
#endif /* NHT_WEBKIT_H */
