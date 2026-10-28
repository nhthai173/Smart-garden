/**
 * @file    nht_sse.h
 * @brief   Bộ tách sự kiện Server-Sent Events, nạp theo từng khối.
 * @version 1.0.0
 * @license MIT
 *
 * Logic thuần, không phụ thuộc ESP-IDF - biên dịch và test được trên máy.
 *
 * Dữ liệu từ socket đến theo khối tuỳ ý, cắt ngang giữa dòng, giữa sự kiện,
 * thậm chí giữa hai byte của một ký tự UTF-8. Bộ tách này giữ trạng thái
 * giữa các lần nạp, nên nơi gọi chỉ việc đổ thẳng những gì đọc được vào.
 *
 * Định dạng một sự kiện:
 *
 *     event: put
 *     data: {"path":"/valve","data":{...}}
 *     <dòng trống>
 *
 * Dòng `data` dài hơn bộ đệm sẽ bị đánh dấu tràn và BỎ QUA cả sự kiện, thay
 * vì đẩy lên trên một chuỗi JSON bị cắt cụt - JSON cụt còn nguy hiểm hơn
 * không có gì, vì nó vẫn có thể parse ra một cấu trúc trông hợp lệ.
 *
 * Một sự kiện nhiều dòng `data` thì dòng cuối thắng (Firebase không dùng
 * kiểu này; đúng chuẩn SSE là nối chúng bằng ký tự xuống dòng).
 */

#ifndef NHT_SSE_H
#define NHT_SSE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NHT_SSE_EVENT_MAX 32

/** Gọi khi gom đủ một sự kiện (đã gặp dòng trống kết thúc). */
typedef void (*nht_sse_cb_t)(const char *event, const char *data, void *user);

typedef struct {
    char   *buf;        /**< bộ đệm dòng do nơi gọi cấp */
    size_t  cap;
    size_t  len;        /**< chỗ ghi tiếp trong buf            */
    size_t  line_start; /**< chỗ bắt đầu dòng đang gom dở      */
    char    event[NHT_SSE_EVENT_MAX];
    char   *data;       /**< trỏ vào buf, hoặc NULL nếu sự kiện chưa có data */
    bool    overflow;
    nht_sse_cb_t cb;
    void   *user;
    uint32_t events;    /**< số sự kiện đã phát - tiện cho test */
    uint32_t dropped;   /**< số sự kiện bị bỏ vì quá dài */
} nht_sse_t;

/**
 * @param buf bộ đệm dòng; phải sống lâu bằng `s`. Nên >= 4096 cho Firebase.
 */
void nht_sse_init(nht_sse_t *s, char *buf, size_t cap,
                  nht_sse_cb_t cb, void *user);

/** Xoá trạng thái dở dang (gọi khi mở lại kết nối). */
void nht_sse_reset(nht_sse_t *s);

/** Nạp một khối byte vừa đọc được từ socket. */
void nht_sse_feed(nht_sse_t *s, const char *chunk, size_t n);

#ifdef __cplusplus
}
#endif
#endif /* NHT_SSE_H */
