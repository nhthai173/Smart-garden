/**
 * @file    nht_pulse.h
 * @brief   Đếm xung bằng PCNT: lưu lượng kế, công tơ, encoder một chiều.
 * @version 1.0.0
 * @license MIT
 *
 * Hai backend, tự chọn theo chip, cùng một API:
 *
 *   - Chip CÓ phần cứng PCNT (ESP32, S2, S3, C6, H2): đếm bằng peripheral.
 *     Bộ đếm PCNT chỉ 16 bit nên thư viện tự cộng dồn mỗi lần tràn qua watch
 *     point; nơi gọi thấy một bộ đếm 64 bit không bao giờ quay vòng. Xung
 *     không bị bỏ sót kể cả khi CPU đang bận.
 *
 *   - Chip KHÔNG có PCNT (ESP32-C3, C2): đếm bằng ngắt GPIO, lọc gai bằng
 *     phần mềm. Với lưu lượng kế thì thừa sức - YF-S201 ở 30 L/phút chỉ
 *     khoảng 225 Hz. Không hợp cho tần số cao hàng chục kHz.
 */

#ifndef NHT_PULSE_H
#define NHT_PULSE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int      gpio;
    uint16_t glitch_ns;        /**< lọc gai ngắn hơn ngần này; 0 -> 1000ns  */
    float    pulses_per_unit;  /**< ví dụ số xung trên mỗi lít; 0 -> 1      */
    bool     pullup;
} nht_pulse_cfg_t;

typedef struct nht_pulse_s nht_pulse_t;

esp_err_t nht_pulse_init(nht_pulse_t **out, const nht_pulse_cfg_t *cfg);
void      nht_pulse_deinit(nht_pulse_t *p);

/** Tổng số xung từ lúc reset gần nhất. */
uint64_t  nht_pulse_total(nht_pulse_t *p);

/** Tổng theo đơn vị vật lý (lít...) = tổng xung / pulses_per_unit. */
float     nht_pulse_volume(nht_pulse_t *p);

/**
 * Tốc độ trung bình tính từ lần gọi trước, đơn vị/phút.
 * Gọi đều đặn (1..5 giây/lần); lần gọi đầu trả 0.
 */
float     nht_pulse_rate(nht_pulse_t *p, uint32_t now_ms);

void      nht_pulse_reset(nht_pulse_t *p);

#ifdef __cplusplus
}
#endif
#endif /* NHT_PULSE_H */
