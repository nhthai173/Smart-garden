/**
 * @file    nht_dinput.h
 * @brief   Đầu vào rời rạc chống nhiễu: Schmitt trigger + trễ bất đối xứng.
 * @version 1.0.0
 * @license MIT
 *
 * Dùng cho mọi cảm biến hai trạng thái mà tín hiệu thật hay lởn vởn quanh
 * ngưỡng: cảm biến ngập nước, phao mức, độ ẩm đất, cảm biến mưa, công tắc
 * cửa, quang trở. Đọc analog hay digital đều dùng chung một API.
 *
 * ---------------------------------------------------------------------------
 * VÌ SAO CẦN HAI LỚP
 *
 * Debounce đối xứng thông thường (giữ N ms cho cả hai chiều) không cứu được
 * cảm biến bị ẩm: tín hiệu dao động quanh MỘT ngưỡng duy nhất, thỉnh thoảng
 * ở yên một bên đủ lâu để vượt debounce, thế là sinh ra một chuỗi đổi trạng
 * thái giả kéo dài hàng giờ.
 *
 *  Lớp 1 - SCHMITT TRIGGER (chỉ analog): hai ngưỡng thay vì một. Đang tắt thì
 *          phải vượt `thr_on`; đang bật thì phải tụt dưới `thr_off`. Vùng
 *          giữa hai ngưỡng không sinh chuyển trạng thái nào.
 *
 *  Lớp 2 - TRỄ BẤT ĐỐI XỨNG: `hold_on_ms` và `hold_off_ms` đặt khác nhau
 *          được. Với cảnh báo an toàn thì vào nhanh - ra chậm (ví dụ 5 giây
 *          và 10 phút): còn ẩm thì vẫn coi như đang có sự cố.
 *
 * Chế độ digital chỉ có lớp 2 - đó là tất cả những gì còn làm được sau khi
 * module comparator đã nuốt mất thông tin mức độ.
 * ---------------------------------------------------------------------------
 *
 * Toàn bộ file này là logic thuần: thời gian truyền vào qua tham số, không
 * gọi millis()/esp_timer. Nhờ vậy test được kịch bản 3 tiếng trong 1 ms.
 */

#ifndef NHT_DINPUT_H
#define NHT_DINPUT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NHT_DIN_INACTIVE = 0,
    NHT_DIN_ACTIVE   = 1,
} nht_din_state_t;

struct nht_dinput_s;
typedef void (*nht_din_cb_t)(struct nht_dinput_s *in, nht_din_state_t st,
                             void *user);

typedef struct nht_dinput_s {
    /* ---- cấu hình ---- */
    bool     analog;       /**< true: `raw` là giá trị ADC; false: mức 0/1   */
    bool     inverted;     /**< true nếu mạch cho giá trị THẤP khi tác động  */
    uint16_t thr_on;       /**< ngưỡng vào ACTIVE   (chỉ analog)             */
    uint16_t thr_off;      /**< ngưỡng ra INACTIVE  (chỉ analog, < thr_on)   */
    uint16_t adc_max;      /**< thang đo ADC, mặc định 4095                  */
    uint32_t hold_on_ms;   /**< phải ACTIVE liên tục bấy nhiêu mới công nhận */
    uint32_t hold_off_ms;  /**< phải INACTIVE liên tục bấy nhiêu mới công nhận*/

    nht_din_cb_t on_change;
    void        *user;

    /* ---- trạng thái (chỉ đọc) ---- */
    nht_din_state_t state;
    nht_din_state_t pending;
    bool     has_pending;
    uint32_t pending_since_ms;
    uint32_t changed_at_ms;
    uint32_t transitions;  /**< tổng số lần đổi trạng thái đã công nhận */
    uint16_t last_raw;
} nht_dinput_t;

#define NHT_DIN_DEFAULT_ADC_MAX   4095u
#define NHT_DIN_DEFAULT_HOLD_MS     50u

/**
 * Chuẩn hoá cấu hình và đặt trạng thái ban đầu.
 * Điền các trường cấu hình vào struct TRƯỚC khi gọi; hàm này giữ nguyên chúng,
 * chỉ điền mặc định cho phần bỏ trống và bảo đảm thr_off < thr_on.
 * @param initial trạng thái coi như đang có lúc khởi động
 */
void nht_dinput_init(nht_dinput_t *in, nht_din_state_t initial);

/**
 * Nạp một mẫu đo.
 * @param raw    giá trị ADC (analog) hoặc mức logic 0/1 (digital)
 * @param now_ms mốc thời gian đơn điệu tăng, đơn vị ms
 * @return true nếu trạng thái CÔNG NHẬN vừa đổi ở đúng lần gọi này
 *         (callback on_change, nếu có, đã được gọi trước khi trả về)
 */
bool nht_dinput_feed(nht_dinput_t *in, uint16_t raw, uint32_t now_ms);

/** Đã ở trạng thái hiện tại được bao lâu (ms). */
uint32_t nht_dinput_stable_ms(const nht_dinput_t *in, uint32_t now_ms);

static inline bool nht_dinput_active(const nht_dinput_t *in)
{
    return in->state == NHT_DIN_ACTIVE;
}

/**
 * Trung vị của mảng mẫu - lọc gai nhiễu trước khi đưa vào feed().
 * Sắp xếp tại chỗ (`buf` bị thay đổi). n lẻ cho kết quả đẹp nhất.
 */
uint16_t nht_median_u16(uint16_t *buf, uint32_t n);

#ifdef __cplusplus
}
#endif
#endif /* NHT_DINPUT_H */
