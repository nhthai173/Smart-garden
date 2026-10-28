/**
 * @file    nht_valve.h
 * @brief   Điều khiển van nước: van bi động cơ và van điện từ thường đóng.
 * @version 1.0.0
 * @license MIT
 *
 * Gói trọn trình tự đóng/mở của hai kiểu van hay gặp trong hệ tưới, sau một
 * API duy nhất. Đổi loại van chỉ là đổi một trường trong cấu hình - phần
 * logic tưới bên trên không cần biết.
 *
 *  NHT_VALVE_SOLENOID - van điện từ thường đóng. Một relay. Cấp điện là mở,
 *      ngắt điện lò xo tự đóng. Mất điện = tự đóng, an toàn miễn phí.
 *
 *  NHT_VALVE_BALL - van bi động cơ. Động cơ quay hết hành trình mất vài
 *      giây; cần chân cấp nguồn cộng chân chọn chiều. Mở được một phần bằng
 *      cách cấp điện trong một phần của hành trình.
 *
 * Tuỳ chọn thêm: một relay bơm (bật trễ sau khi van bắt đầu mở) và một relay
 * nguồn AC tổng (tự cắt sau khi van đứng yên).
 *
 * AN TOÀN
 *  - `max_run_ms` là trần cứng. Không lệnh nào vượt qua được, kể cả lịch hẹn
 *    hay lệnh từ cloud. Đây là lớp chặn cuối nếu tầng trên tính sai.
 *  - `nht_valve_init()` luôn ĐÓNG van. Với van bi nghĩa là chạy đủ một chu
 *    trình đóng, không phải chỉ ngắt relay: sau khi thiết bị tự reset giữa
 *    lúc đang tưới, không ai biết van đang ở đâu.
 *  - Khoá an toàn (`inhibit`) đóng van ngay và chặn mọi lệnh mở.
 *
 * Thời gian truyền vào qua tham số `now_ms`, không hàm nào tự gọi đồng hồ -
 * test được toàn bộ trình tự trên máy.
 */

#ifndef NHT_VALVE_H
#define NHT_VALVE_H

#include <stdint.h>
#include <stdbool.h>
#include "nht_actuator.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NHT_VALVE_SOLENOID = 0,
    NHT_VALVE_BALL     = 1,
} nht_valve_type_t;

typedef enum {
    NHT_VALVE_CLOSED = 0,
    NHT_VALVE_OPENING,
    NHT_VALVE_OPEN,
    NHT_VALVE_CLOSING,
} nht_valve_state_t;

typedef enum {
    NHT_VALVE_OK = 0,
    NHT_VALVE_ERR_INHIBIT,
    NHT_VALVE_ERR_BUSY,     /**< động cơ đang quay, đợi xong đã */
    NHT_VALVE_ERR_PARAM,
} nht_valve_err_t;

typedef enum {
    NHT_VALVE_EV_OPENED = 0,
    NHT_VALVE_EV_CLOSED,
    NHT_VALVE_EV_AUTO_CLOSED,  /**< đóng do hết thời lượng */
    NHT_VALVE_EV_INHIBITED,    /**< đóng do khoá an toàn   */
} nht_valve_event_t;

struct nht_valve_s;
typedef struct nht_valve_s nht_valve_t;
typedef void (*nht_valve_evt_cb_t)(nht_valve_t *v, nht_valve_event_t ev,
                                   void *user);

typedef struct {
    nht_valve_type_t type;

    int  pin_power;        /**< van bi: nguồn động cơ. solenoid: cuộn van   */
    int  pin_dir;          /**< van bi: chọn chiều (bật = đóng). -1 = không */
    int  pin_pump;         /**< relay bơm, -1 = không có                    */
    int  pin_ac;           /**< relay nguồn AC tổng, -1 = không có          */
    bool active_high;      /**< false = relay kích mức thấp                 */

    uint32_t travel_ms;       /**< van bi: thời gian quay hết hành trình    */
    uint32_t pump_delay_ms;   /**< bơm bật sau khi van bắt đầu mở           */
    uint32_t ac_auto_off_ms;  /**< tự cắt AC sau bấy nhiêu; 0 = không cắt   */
    uint32_t default_run_ms;  /**< thời lượng khi mở không nêu rõ           */
    uint32_t max_run_ms;      /**< TRẦN CỨNG, không lệnh nào vượt được      */
    uint32_t reverse_settle_ms; /**< nghỉ giữa lúc ngắt động cơ và lúc đổi
                                     chiều; 0 = 100 ms                        */

    nht_act_write_t write;    /**< hook ghi chân; NULL = dùng GPIO của IDF  */
    void           *user;
} nht_valve_cfg_t;

struct nht_valve_s {
    nht_valve_cfg_t cfg;
    nht_valve_state_t state;

    nht_act_t run;    /**< đầu ra ảo giữ thời lượng tưới + tự tắt */
    nht_act_t power;
    nht_act_t dir;
    nht_act_t pump;
    nht_act_t ac;

    uint32_t now_ms;          /**< mốc thời gian của lệnh đang xử lý */
    uint32_t travel_until_ms;
    bool     travelling;
    uint8_t  level_pct;
    bool     inhibit;

    /* Đảo chiều giữa hành trình: ngắt động cơ, nghỉ, rồi mới đổi chiều. */
    bool     move_pending;
    bool     move_close;
    uint32_t move_ms;
    uint32_t move_at_ms;

    nht_valve_evt_cb_t on_event;
    void              *evt_user;
};

/** Chuẩn bị van và đưa về trạng thái ĐÓNG chắc chắn. */
nht_valve_err_t nht_valve_init(nht_valve_t *v, const nht_valve_cfg_t *cfg,
                               uint32_t now_ms);

void nht_valve_on_event(nht_valve_t *v, nht_valve_evt_cb_t cb, void *user);

/**
 * Mở van trong `run_ms` mili giây.
 * @param run_ms    0 = dùng `default_run_ms`. Luôn bị kẹp xuống `max_run_ms`.
 * @param level_pct 1..100, chỉ có tác dụng với van bi. 0 = coi như 100.
 *
 * Van ĐANG MỞ thì chỉ đặt lại thời lượng, không quay lại động cơ. Gọi lặp
 * cùng tham số là vô hại.
 */
nht_valve_err_t nht_valve_open(nht_valve_t *v, uint32_t run_ms,
                               uint8_t level_pct, uint32_t now_ms);

nht_valve_err_t nht_valve_close(nht_valve_t *v, uint32_t now_ms);

/** Đóng van bất chấp mọi ràng buộc. Dùng cho sự cố. */
nht_valve_err_t nht_valve_close_force(nht_valve_t *v, uint32_t now_ms);

/** Khoá: đóng van ngay và chặn mọi lệnh mở tới khi mở khoá. */
void nht_valve_set_inhibit(nht_valve_t *v, bool inhibit, uint32_t now_ms);

/** Gọi khi tới hạn (xem nht_valve_next_wake_ms) hoặc sau mỗi lệnh. */
void nht_valve_tick(nht_valve_t *v, uint32_t now_ms);

/**
 * Bao lâu nữa van cần được tick: hết hành trình, hết thời lượng, hết trễ
 * bơm/AC. false = van đứng yên, không có hạn nào. Cho phép chờ theo sự kiện
 * thay vì tick cố định; tick sớm hơn vẫn vô hại.
 */
bool nht_valve_next_wake_ms(const nht_valve_t *v, uint32_t now_ms,
                            uint32_t *out_ms);

uint32_t nht_valve_remaining_ms(const nht_valve_t *v, uint32_t now_ms);
const char *nht_valve_state_str(nht_valve_state_t s);

static inline nht_valve_state_t nht_valve_state(const nht_valve_t *v)
{
    return v->state;
}
/** true khi van đang mở HOẶC đang quay để mở. */
static inline bool nht_valve_is_flowing(const nht_valve_t *v)
{
    return v->state == NHT_VALVE_OPEN || v->state == NHT_VALVE_OPENING;
}

#ifdef __cplusplus
}
#endif
#endif /* NHT_VALVE_H */
