/**
 * @file    nht_sched.h
 * @brief   Lịch theo giờ-trong-ngày, có bù giờ. Logic thuần, không phụ thuộc
 *          ESP-IDF - biên dịch và test được trên máy tính.
 * @version 1.0.0
 * @license MIT
 *
 * Thư viện không biết gì về việc nó đang hẹn giờ cho cái gì: mỗi task mang
 * theo một `payload` thô do ứng dụng tự định nghĩa. Tưới cây thì payload là
 * {thời lượng, mức mở van}; dự án khác có thể nhét bất cứ thứ gì vừa kích cỡ.
 *
 * ĐẶT KÍCH CỠ PAYLOAD (platformio.ini hoặc Kconfig):
 *     build_flags = -D NHT_SCHED_PAYLOAD_SIZE=8
 *     build_flags = -D NHT_SCHED_MAX_TASKS=16
 *
 * ---------------------------------------------------------------------------
 * VÌ SAO CÓ CỬA SỔ BÙ GIỜ
 *
 * Cách hẹn giờ ngây thơ là so khớp đúng phút hiện tại (`hour==h && minute==m`).
 * Nó hỏng ngay khi vòng lặp chính bị trễ quá một phút - OTA, chờ mạng, TLS
 * handshake - và khi đó lịch của ngày hôm ấy mất luôn, im lặng, không dấu vết.
 *
 * Ở đây một task tới hạn vẫn chạy được trong `catchup_window_s` giây sau giờ
 * hẹn, còn `last_run_day` đảm bảo nó chỉ chạy đúng một lần mỗi ngày.
 * ---------------------------------------------------------------------------
 */

#ifndef NHT_SCHED_H
#define NHT_SCHED_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NHT_SCHED_MAX_TASKS
#define NHT_SCHED_MAX_TASKS 16
#endif

#ifndef NHT_SCHED_PAYLOAD_SIZE
#define NHT_SCHED_PAYLOAD_SIZE 8
#endif

#define NHT_SCHED_DEFAULT_CATCHUP_S 600u  /* 10 phút */

/** bit0 = Chủ nhật ... bit6 = Thứ bảy. */
#define NHT_SCHED_REPEAT_NONE   0x00u   /* chạy một lần rồi tự tắt */
#define NHT_SCHED_REPEAT_DAILY  0x7Fu
#define NHT_SCHED_REPEAT_WEEKDAY 0x3Eu  /* T2..T6 */
#define NHT_SCHED_REPEAT_WEEKEND 0x41u  /* CN, T7 */

/** Kiểm tra lúc biên dịch rằng struct payload của ứng dụng vừa chỗ. */
#define NHT_SCHED_ASSERT_PAYLOAD(type)                                    \
    typedef char nht_sched_payload_too_big_##type[                        \
        (sizeof(type) <= NHT_SCHED_PAYLOAD_SIZE) ? 1 : -1]

typedef struct {
    uint8_t  id;            /**< 1..255; 0 = ô trống                        */
    uint8_t  hour;          /**< 0..23                                      */
    uint8_t  minute;        /**< 0..59                                      */
    uint8_t  repeat_mask;   /**< NHT_SCHED_REPEAT_*                         */
    bool     enabled;
    uint32_t last_run_day;  /**< số ngày kể từ epoch của lần chạy gần nhất  */
    uint8_t  payload[NHT_SCHED_PAYLOAD_SIZE];
} nht_sched_task_t;

typedef struct {
    nht_sched_task_t tasks[NHT_SCHED_MAX_TASKS];
    uint8_t          count;
    uint8_t          next_id;
    uint16_t         catchup_window_s;
} nht_sched_t;

/**
 * Thời gian đã tách sẵn theo giờ địa phương.
 * Truyền struct này thay vì time_t để test trên máy không phải đụng tới
 * timezone của hệ điều hành.
 */
typedef struct {
    uint32_t day;    /**< số ngày kể từ epoch (giờ ĐỊA PHƯƠNG); 0 = chưa có giờ */
    uint8_t  wday;   /**< 0=CN .. 6=T7 */
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
} nht_time_t;

/** Đổi unix timestamp + offset múi giờ (giây, ví dụ 7*3600) sang nht_time_t. */
void nht_time_from_unix(int64_t unix_s, int32_t tz_offset_s, nht_time_t *out);

/** @param catchup_window_s 0 = dùng mặc định (600s). */
void nht_sched_init(nht_sched_t *s, uint16_t catchup_window_s);

/** Thêm task. Trường `id` của `t` bị bỏ qua; id mới trả qua `out_id`. */
bool nht_sched_add(nht_sched_t *s, const nht_sched_task_t *t, uint8_t *out_id);

/**
 * Sửa task theo `t->id`, sửa TRỰC TIẾP vào mảng.
 * `last_run_day` được giữ, trừ khi giờ hẹn đổi thì reset để hôm nay chạy lại.
 */
bool nht_sched_update(nht_sched_t *s, const nht_sched_task_t *t);

bool nht_sched_remove(nht_sched_t *s, uint8_t id);
bool nht_sched_set_enabled(nht_sched_t *s, uint8_t id, bool enabled);
void nht_sched_clear(nht_sched_t *s);

nht_sched_task_t *nht_sched_find(nht_sched_t *s, uint8_t id);

/**
 * Lấy một task tới hạn tại `now`, đánh dấu đã chạy trong ngày.
 * @return id (>0) và chép task vào `out`; 0 nếu không có gì tới hạn.
 * Gọi lặp cho tới khi trả 0 để vét hết nhiều lịch cùng tới hạn.
 */
uint8_t nht_sched_due(nht_sched_t *s, const nht_time_t *now,
                      nht_sched_task_t *out);

/** Số giây tới lần chạy kế tiếp của bất kỳ task nào; UINT32_MAX nếu không có. */
uint32_t nht_sched_next_in(const nht_sched_t *s, const nht_time_t *now);

static inline void *nht_sched_payload(nht_sched_task_t *t) { return t->payload; }

static inline void nht_sched_set_payload(nht_sched_task_t *t,
                                         const void *src, uint32_t len)
{
    memset(t->payload, 0, NHT_SCHED_PAYLOAD_SIZE);
    if (src && len) {
        memcpy(t->payload, src,
               len > NHT_SCHED_PAYLOAD_SIZE ? NHT_SCHED_PAYLOAD_SIZE : len);
    }
}

#ifdef __cplusplus
}
#endif
#endif /* NHT_SCHED_H */
