/**
 * @file    nht_actuator.h
 * @brief   Đầu ra có trạng thái: tự tắt theo thời lượng, trễ bật, chống đảo
 *          nhanh, khoá an toàn. Bản ESP-IDF của GenericOutput/VirtualOutput.
 * @version 1.0.0
 * @license MIT
 *
 * Một `nht_act_t` trông coi một relay / MOSFET / đầu ra ảo. Việc ghi mức
 * logic đi qua một hook, nên cũng gắn được vào GPIO expander (PCF8574),
 * thanh ghi dịch, hay một biến trong RAM khi chạy test.
 *
 * Toàn bộ mốc thời gian truyền vào qua tham số `now_ms`, không hàm nào tự
 * gọi đồng hồ. Đổi lại việc phải truyền thêm một tham số, ta có: test được
 * kịch bản nhiều giờ trong vài mili giây, và không có mảng thời gian nào
 * lén thay đổi sau lưng.
 *
 * ---------------------------------------------------------------------------
 * NGỮ NGHĨA CỦA nht_act_on_for() - ĐỌC KỸ CHỖ NÀY
 *
 * Đây là chỗ dễ hỏng nhất, và nó đã hỏng thật ở bản Arduino trước:
 *
 *   `on()` khi đầu ra ĐANG bật lại xoá thời lượng một-lần đã đặt và dời mốc
 *   đếm giờ về hiện tại - làm việc đó TRƯỚC khi thoát sớm vì "đã bật rồi".
 *   Chỉ cần một lệnh bật lặp lại từ đâu đó (một event đồng bộ dội về chẳng
 *   hạn) là thời lượng 15 phút âm thầm tụt về mặc định 1 phút.
 *
 * Ở đây, khi đầu ra đang bật:
 *
 *   - `nht_act_on_for(a, ms)` chỉ ĐẶT LẠI HẠN thành `mốc_bật + ms`. Không
 *     chạy lại trình tự bật, không dời mốc bật. Nghĩa là gọi lặp lại cùng
 *     một lệnh KHÔNG thay đổi gì hết - lệnh trùng là vô hại theo thiết kế.
 *   - `nht_act_on(a)` KHÔNG đụng tới thời lượng một-lần đang chạy.
 *   - Muốn cộng thêm thời gian tính từ bây giờ thì gọi `nht_act_extend()`.
 * ---------------------------------------------------------------------------
 */

#ifndef NHT_ACTUATOR_H
#define NHT_ACTUATOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NHT_ACT_OFF = 0,
    NHT_ACT_PENDING_ON,   /**< đã nhận lệnh bật, đang đợi hết on_delay_ms */
    NHT_ACT_ON,
} nht_act_state_t;

typedef enum {
    NHT_ACT_OK = 0,
    NHT_ACT_ERR_INHIBIT,  /**< đang bị khoá an toàn                        */
    NHT_ACT_ERR_MIN_OFF,  /**< vừa tắt xong, chưa đủ min_off_ms            */
    NHT_ACT_ERR_MIN_ON,   /**< vừa bật xong, chưa đủ min_on_ms             */
    NHT_ACT_ERR_PARAM,
} nht_act_err_t;

typedef enum {
    NHT_ACT_EV_ON = 0,    /**< đã thực sự đóng mạch (sau on_delay)         */
    NHT_ACT_EV_OFF,       /**< tắt do lệnh                                 */
    NHT_ACT_EV_AUTO_OFF,  /**< tắt do hết thời lượng                       */
    NHT_ACT_EV_INHIBITED, /**< bị khoá an toàn nên cưỡng bức tắt           */
} nht_act_event_t;

typedef enum {
    NHT_ACT_START_OFF = 0,
    NHT_ACT_START_ON,
} nht_act_startup_t;

struct nht_act_s;
typedef struct nht_act_s nht_act_t;

/** Ghi mức logic THẬT ra chân (đã tính active_high). NULL = dùng GPIO IDF. */
typedef void (*nht_act_write_t)(int pin, bool level, void *user);
typedef void (*nht_act_evt_cb_t)(nht_act_t *a, nht_act_event_t ev, void *user);

typedef struct {
    int      pin;          /**< số GPIO; < 0 nghĩa là đầu ra ảo            */
    bool     active_high;  /**< false = relay kích mức thấp                */
    uint32_t auto_off_ms;  /**< thời lượng mặc định; 0 = bật là giữ mãi    */
    uint32_t on_delay_ms;  /**< trễ giữa lệnh bật và lúc đóng mạch thật    */
    uint32_t min_on_ms;    /**< tối thiểu phải bật bấy nhiêu mới cho tắt   */
    uint32_t min_off_ms;   /**< tối thiểu phải tắt bấy nhiêu mới cho bật   */
    nht_act_startup_t startup;
    nht_act_write_t   write;
    void             *user;
    const char       *name; /**< dùng cho log; có thể NULL                 */
} nht_act_cfg_t;

struct nht_act_s {
    nht_act_cfg_t   cfg;
    nht_act_state_t state;
    bool     inhibit;
    uint32_t once_ms;         /**< thời lượng của lượt bật hiện tại        */
    uint32_t state_since_ms;
    uint32_t on_at_ms;        /**< mốc đóng mạch thật, để tính hạn tắt     */
    uint32_t deadline_ms;
    bool     has_deadline;
    uint32_t cmd_at_ms;       /**< mốc nhận lệnh bật (dùng cho on_delay)   */
    nht_act_evt_cb_t on_event;
    void            *evt_user;
};

/** Chuẩn bị struct và đưa chân về trạng thái khởi động. */
nht_act_err_t nht_act_init(nht_act_t *a, const nht_act_cfg_t *cfg,
                           uint32_t now_ms);

void nht_act_on_event(nht_act_t *a, nht_act_evt_cb_t cb, void *user);

/** Bật với `auto_off_ms` mặc định. Đang bật thì KHÔNG đụng tới hạn hiện có. */
nht_act_err_t nht_act_on(nht_act_t *a, uint32_t now_ms);

/**
 * Bật trong đúng `ms` mili giây tính từ lúc ĐÓNG MẠCH.
 * `ms == 0` nghĩa là dùng `auto_off_ms` mặc định.
 * Đang bật thì chỉ đặt lại hạn - gọi lặp cùng tham số là vô hại.
 */
nht_act_err_t nht_act_on_for(nht_act_t *a, uint32_t ms, uint32_t now_ms);

/**
 * Bật nếu đang tắt; đang bật thì tính lại hạn tự tắt từ BÂY GIỜ (không bật
 * lại, không glitch chân). Dùng cho đầu ra kiểu "giữ thêm một lúc sau lần
 * hoạt động cuối", như nguồn AC của van.
 */
nht_act_err_t nht_act_retrigger(nht_act_t *a, uint32_t now_ms);

/** Cộng thêm `ms` vào hạn hiện tại, tính từ BÂY GIỜ. */
nht_act_err_t nht_act_extend(nht_act_t *a, uint32_t ms, uint32_t now_ms);

nht_act_err_t nht_act_off(nht_act_t *a, uint32_t now_ms);

/** Tắt bất chấp `min_on_ms`. Dùng cho dừng khẩn cấp / khoá an toàn. */
nht_act_err_t nht_act_off_force(nht_act_t *a, uint32_t now_ms);

nht_act_err_t nht_act_set(nht_act_t *a, bool on, uint32_t now_ms);

/**
 * Đồng bộ theo trạng thái báo về từ nơi khác, CÓ chốt chặn: đã đúng trạng
 * thái thì không làm gì cả.
 *
 * Đây là hàm phải dùng khi nhận trạng thái từ cloud/đồng bộ - không phải
 * nht_act_set(). Chính chỗ này là nguyên nhân lỗi "tưới sai thời lượng" ở
 * bản trước: nhánh xử lý cập nhật lẻ gọi thẳng set() không so sánh gì.
 */
nht_act_err_t nht_act_sync(nht_act_t *a, bool on, uint32_t now_ms);

/** Bật khoá: cưỡng bức tắt ngay và từ chối mọi lệnh bật cho tới khi mở khoá. */
void nht_act_set_inhibit(nht_act_t *a, bool inhibit, uint32_t now_ms);

/** Phải gọi đều đặn (10..100 ms/lần) để on_delay và auto-off có hiệu lực. */
void nht_act_tick(nht_act_t *a, uint32_t now_ms);

/**
 * Bao lâu nữa thì cần gọi nht_act_tick() để có việc xảy ra (hết trễ bật hoặc
 * hết hạn tự tắt). false = không có hạn nào, ngủ bao lâu cũng được.
 * Dùng để chờ theo sự kiện thay vì tick đều đặn.
 */
bool nht_act_next_wake_ms(const nht_act_t *a, uint32_t now_ms, uint32_t *out_ms);

/** Còn lại bao nhiêu ms trước khi tự tắt; 0 nếu không có hạn hoặc đã tắt. */
uint32_t nht_act_remaining_ms(const nht_act_t *a, uint32_t now_ms);

static inline bool nht_act_is_on(const nht_act_t *a)
{
    return a->state == NHT_ACT_ON;
}
static inline bool nht_act_is_inhibited(const nht_act_t *a)
{
    return a->inhibit;
}
const char *nht_act_state_str(nht_act_state_t s);

#ifdef __cplusplus
}
#endif
#endif /* NHT_ACTUATOR_H */
