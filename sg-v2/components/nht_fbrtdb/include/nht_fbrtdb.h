/**
 * @file    nht_fbrtdb.h
 * @brief   Firebase Realtime Database client cho ESP-IDF: đăng nhập bằng
 *          email/mật khẩu, REST CRUD, và stream SSE.
 * @version 1.0.0
 * @license MIT
 *
 * Không có thư viện Firebase chính thức nào cho ESP-IDF (các thư viện đang
 * dùng rộng rãi đều viết cho Arduino), nên phần này tự dựng trên
 * `esp_http_client` + `esp_crt_bundle`.
 *
 * NHỮNG CHỖ DỄ SAI, ĐÃ XỬ LÝ Ở ĐÂY
 *
 *  1. CHUYỂN HƯỚNG 307. Database đời cũ (*.firebaseio.com) KHÔNG phục vụ
 *     stream ở host trong URL bạn đưa: nó trả 307 kèm header Location trỏ
 *     sang một máy chủ khác (s-xxx-nss-yyy.firebaseio.com). Database vùng
 *     mới (*.firebasedatabase.app) phục vụ thẳng, không chuyển hướng.
 *     Firmware phải chịu được cả hai.
 *
 *     Điểm mấu chốt: ở chế độ mở kết nối thủ công - bắt buộc phải dùng để
 *     đọc stream - esp_http_client KHÔNG tự đi theo chuyển hướng, dù cấu
 *     hình có bật. Gặp database đời cũ mà không xử lý thì stream mở ra rồi
 *     im lặng vĩnh viễn, không có lỗi nào để lần.
 *
 *  2. TOKEN HẾT HẠN GIỮA CHỪNG. idToken sống một tiếng. Stream đang mở sẽ
 *     bị cắt khi token hết hạn, nên token được làm mới trước hạn 5 phút và
 *     stream chủ động mở lại với token mới.
 *
 *  3. ĐỌC THEO KHỐI. Đọc từng byte một cho đơn giản nhưng ngốn CPU vô ích;
 *     ở đây đọc theo khối rồi mới tách dòng.
 *
 *  4. GHI KHÔNG CHẶN. Các hàm ghi đẩy việc vào hàng đợi cho task riêng làm.
 *     Task điều khiển van không bao giờ được đứng chờ TLS bắt tay.
 */

#ifndef NHT_FBRTDB_H
#define NHT_FBRTDB_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NHT_FB_OFFLINE = 0,
    NHT_FB_AUTH,        /**< đang đăng nhập / làm mới token */
    NHT_FB_READY,
    NHT_FB_ERROR,
} nht_fb_state_t;

typedef struct {
    const char *api_key;
    const char *db_url;      /**< https://<project>.firebaseio.com (không / cuối) */
    const char *email;
    const char *password;
    uint16_t    queue_depth; /**< 0 -> 8  */
    uint32_t    retry_ms;    /**< 0 -> 5000 */
} nht_fb_cfg_t;

/**
 * @param event  "put" | "patch" | "keep-alive" | "cancel" | "auth_revoked"
 *               | "connected" (tự phát mỗi lần stream mở được, kể cả mở lại
 *               sau khi đứt hoặc đổi token; ngay sau đó là một "put" "/"
 *               mang BẢN CHỤP trạng thái hiện tại - không phải thay đổi mới)
 * @param path   đường dẫn tương đối so với path đang stream, ví dụ "/valve"
 * @param data   JSON thô của trường "data" (có thể là "null")
 */
typedef void (*nht_fb_stream_cb_t)(const char *event, const char *path,
                                   const char *data, void *user);
typedef void (*nht_fb_state_cb_t)(nht_fb_state_t st, void *user);

esp_err_t      nht_fb_start(const nht_fb_cfg_t *cfg);
void           nht_fb_stop(void);
nht_fb_state_t nht_fb_state(void);
void           nht_fb_on_state(nht_fb_state_cb_t cb, void *user);

/** uid của tài khoản sau khi đăng nhập; chuỗi rỗng nếu chưa đăng nhập. */
const char *nht_fb_uid(void);

/* --- ghi bất đồng bộ: trả về ngay, task nền thực hiện --- */
esp_err_t nht_fb_put(const char *path, const char *json);
esp_err_t nht_fb_patch(const char *path, const char *json);
esp_err_t nht_fb_delete(const char *path);

/* --- đồng bộ: chặn cho tới khi xong. KHÔNG gọi từ task điều khiển. --- */
esp_err_t nht_fb_get_sync(const char *path, char *out, size_t out_len);
esp_err_t nht_fb_put_sync(const char *path, const char *json);

/**
 * Mở stream SSE trên `path`. Chỉ hỗ trợ một stream tại một thời điểm - đủ
 * cho mô hình desired/reported và tiết kiệm một kết nối TLS trên chip nhỏ.
 */
esp_err_t nht_fb_stream_start(const char *path, nht_fb_stream_cb_t cb,
                              void *user);
void      nht_fb_stream_stop(void);
bool      nht_fb_stream_connected(void);

#ifdef __cplusplus
}
#endif
#endif /* NHT_FBRTDB_H */
