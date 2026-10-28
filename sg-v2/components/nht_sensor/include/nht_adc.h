/**
 * @file    nht_adc.h
 * @brief   Đọc ADC one-shot có hiệu chuẩn và lọc trung vị.
 * @version 1.0.0
 * @license MIT
 *
 * Bọc `adc_oneshot` + `adc_cali` của ESP-IDF lại thành một API ngắn, thêm
 * lọc trung vị (gai nhiễu bị loại thay vì được lấy trung bình vào kết quả)
 * và một phép quy đổi tuyến tính ra đơn vị vật lý.
 *
 * Ánh xạ GPIO -> kênh ADC do thư viện tự tra, nên nơi gọi chỉ cần biết số
 * chân trong sơ đồ mạch.
 */

#ifndef NHT_ADC_H
#define NHT_ADC_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int         gpio;
    adc_atten_t atten;    /**< mặc định ADC_ATTEN_DB_12 (dải rộng nhất) */
    uint8_t     samples;  /**< số mẫu lấy trung vị; 0 -> 5. Nên là số lẻ  */
    float       scale;    /**< nhân vào điện áp (V) để ra đơn vị vật lý   */
    float       offset;   /**< cộng sau khi nhân                          */
} nht_adc_cfg_t;

typedef struct {
    nht_adc_cfg_t        cfg;
    adc_oneshot_unit_handle_t unit;
    adc_cali_handle_t    cali;
    adc_unit_t           unit_id;
    adc_channel_t        channel;
    bool                 calibrated;
    bool                 owns_unit;
} nht_adc_t;

#define NHT_ADC_MAX_SAMPLES 9

/**
 * Nhiều cảm biến dùng chung một bộ ADC là bình thường - thư viện tự chia sẻ
 * handle phần cứng và đếm tham chiếu. ESP-IDF chỉ cho một handle mỗi bộ, nếu
 * không cảm biến thứ hai sẽ bị từ chối với "adc1 is already in use".
 */
esp_err_t nht_adc_init(nht_adc_t *a, const nht_adc_cfg_t *cfg);
void      nht_adc_deinit(nht_adc_t *a);

/** Giá trị thô 0..4095 sau lọc trung vị. */
esp_err_t nht_adc_read_raw(nht_adc_t *a, uint16_t *raw);

/** Điện áp tại chân, mV. Nếu chip không hiệu chuẩn được thì ước lượng thẳng. */
esp_err_t nht_adc_read_mv(nht_adc_t *a, int *mv);

/** Đại lượng vật lý: (mV/1000) * scale + offset. */
esp_err_t nht_adc_read_value(nht_adc_t *a, float *out);

#if defined(NHT_ADC_FAKE)
/** Chỉ có khi build với -D NHT_ADC_FAKE: đặt giá trị giả cho mô phỏng. */
void nht_adc_fake_set(int gpio, uint16_t v);
#endif

#ifdef __cplusplus
}
#endif
#endif /* NHT_ADC_H */
