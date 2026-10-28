/**
 * @file    nht_store.h
 * @brief   Lưu cấu hình kiểu khoá-giá trị trên NVS. Bản ESP-IDF của ENVFile.
 * @version 1.0.0
 * @license MIT
 *
 * Vì sao NVS chứ không phải file trên LittleFS/SPIFFS: NVS đã có sẵn
 * wear-leveling và ghi theo giao dịch, chịu được mất điện giữa chừng. Một
 * hệ tưới cây bị cắt điện bất kỳ lúc nào không nên đánh cược cấu hình vào
 * một filesystem có thể hỏng cấu trúc.
 *
 * Mọi getter đều nhận giá trị mặc định và không bao giờ báo lỗi: khoá thiếu
 * hay kiểu sai đều trả về mặc định. Nhờ vậy firmware chạy được ngay từ NVS
 * trống, không cần bước "khởi tạo cấu hình".
 */

#ifndef NHT_STORE_H
#define NHT_STORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "nvs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    nvs_handle_t handle;
    bool         opened;
    bool         deferred;   /**< true = gom ghi, phải gọi commit() thủ công */
    char         ns[16];
    char         part[16];
} nht_store_t;

/**
 * @param partition tên partition NVS; NULL = "nvs" mặc định
 * @param ns        namespace, tối đa 15 ký tự
 */
esp_err_t nht_store_init(nht_store_t *st, const char *partition, const char *ns);
void      nht_store_deinit(nht_store_t *st);

/** true = các set_* chỉ ghi vào bộ đệm cho tới khi gọi nht_store_commit(). */
void      nht_store_set_deferred(nht_store_t *st, bool deferred);
esp_err_t nht_store_commit(nht_store_t *st);

bool      nht_store_has(nht_store_t *st, const char *key);
esp_err_t nht_store_remove(nht_store_t *st, const char *key);
esp_err_t nht_store_erase_all(nht_store_t *st);

int32_t   nht_store_get_i32  (nht_store_t *st, const char *key, int32_t  def);
uint32_t  nht_store_get_u32  (nht_store_t *st, const char *key, uint32_t def);
bool      nht_store_get_bool (nht_store_t *st, const char *key, bool     def);
float     nht_store_get_float(nht_store_t *st, const char *key, float    def);

/** @return số ký tự đã chép (không tính '\0'). Thiếu khoá -> chép `def`. */
size_t    nht_store_get_str(nht_store_t *st, const char *key,
                            char *out, size_t out_size, const char *def);

/** @param len vào: kích thước bộ đệm; ra: số byte thực đọc được. */
esp_err_t nht_store_get_blob(nht_store_t *st, const char *key,
                             void *out, size_t *len);

esp_err_t nht_store_set_i32  (nht_store_t *st, const char *key, int32_t  v);
esp_err_t nht_store_set_u32  (nht_store_t *st, const char *key, uint32_t v);
esp_err_t nht_store_set_bool (nht_store_t *st, const char *key, bool     v);
esp_err_t nht_store_set_float(nht_store_t *st, const char *key, float    v);
esp_err_t nht_store_set_str  (nht_store_t *st, const char *key, const char *v);
esp_err_t nht_store_set_blob (nht_store_t *st, const char *key,
                              const void *v, size_t len);

#ifdef __cplusplus
}
#endif
#endif /* NHT_STORE_H */
