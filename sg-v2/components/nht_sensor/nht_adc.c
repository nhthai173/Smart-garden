#include "nht_adc.h"
#include "esp_log.h"
#include <string.h>

#if SOC_ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
#include "esp_adc/adc_cali_scheme.h"
#endif

static const char *TAG = "nht_adc";

/*
 * ESP-IDF chỉ cho phép MỘT handle trên mỗi bộ ADC. Dự án nào cũng thường có
 * vài cảm biến analog cùng nằm trên ADC1 (chỉ ADC1 dùng được khi WiFi bật),
 * nên thư viện tự chia sẻ handle và đếm tham chiếu. Không có phần này thì
 * cảm biến thứ hai lặng lẽ chết với lỗi "adc1 is already in use".
 *
 * Đếm tham chiếu không có khoá: khởi tạo cảm biến là việc làm lúc dựng hệ
 * thống, chạy trên một luồng duy nhất.
 */
static struct {
    adc_oneshot_unit_handle_t handle;
    int refs;
} SHARED[SOC_ADC_PERIPH_NUM];

static esp_err_t unit_acquire(adc_unit_t id, adc_oneshot_unit_handle_t *out)
{
    if (SHARED[id].refs > 0) {
        SHARED[id].refs++;
        *out = SHARED[id].handle;
        return ESP_OK;
    }

    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = id };
    esp_err_t err = adc_oneshot_new_unit(&ucfg, &SHARED[id].handle);
    if (err != ESP_OK) return err;

    SHARED[id].refs = 1;
    *out = SHARED[id].handle;
    return ESP_OK;
}

static void unit_release(adc_unit_t id)
{
    if (SHARED[id].refs <= 0) return;
    if (--SHARED[id].refs == 0) {
        adc_oneshot_del_unit(SHARED[id].handle);
        SHARED[id].handle = NULL;
    }
}

static uint16_t median_u16(uint16_t *b, uint32_t n)
{
    for (uint32_t i = 1; i < n; i++) {
        uint16_t v = b[i];
        uint32_t j = i;
        while (j > 0 && b[j - 1] > v) { b[j] = b[j - 1]; j--; }
        b[j] = v;
    }
    return b[n / 2];
}

esp_err_t nht_adc_init(nht_adc_t *a, const nht_adc_cfg_t *cfg)
{
    if (!a || !cfg) return ESP_ERR_INVALID_ARG;
    memset(a, 0, sizeof(*a));
    a->cfg = *cfg;
    if (!a->cfg.samples) a->cfg.samples = 5;
    if (a->cfg.samples > NHT_ADC_MAX_SAMPLES)
        a->cfg.samples = NHT_ADC_MAX_SAMPLES;
    if (a->cfg.scale == 0.0f) a->cfg.scale = 1.0f;
    if (a->cfg.atten == 0) a->cfg.atten = ADC_ATTEN_DB_12;

    esp_err_t err = adc_oneshot_io_to_channel(cfg->gpio, &a->unit_id,
                                              &a->channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d không phải chân ADC", cfg->gpio);
        return err;
    }

    err = unit_acquire(a->unit_id, &a->unit);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "không mở được ADC%d: %s", a->unit_id + 1,
                 esp_err_to_name(err));
        return err;
    }
    a->owns_unit = true;

    adc_oneshot_chan_cfg_t ccfg = {
        .atten    = a->cfg.atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(a->unit, a->channel, &ccfg);
    if (err != ESP_OK) return err;

#if SOC_ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cal = {
        .unit_id  = a->unit_id,
        .chan     = a->channel,
        .atten    = a->cfg.atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    a->calibrated = (adc_cali_create_scheme_curve_fitting(&cal, &a->cali)
                     == ESP_OK);
#elif SOC_ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cal = {
        .unit_id  = a->unit_id,
        .atten    = a->cfg.atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    a->calibrated = (adc_cali_create_scheme_line_fitting(&cal, &a->cali)
                     == ESP_OK);
#endif
    if (!a->calibrated)
        ESP_LOGW(TAG, "GPIO%d: không hiệu chuẩn được, dùng ước lượng tuyến tính",
                 cfg->gpio);

    return ESP_OK;
}

void nht_adc_deinit(nht_adc_t *a)
{
    if (!a) return;
#if SOC_ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (a->calibrated) adc_cali_delete_scheme_curve_fitting(a->cali);
#elif SOC_ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (a->calibrated) adc_cali_delete_scheme_line_fitting(a->cali);
#endif
    if (a->owns_unit) unit_release(a->unit_id);
    a->owns_unit = false;
    a->calibrated = false;
}

#if defined(NHT_ADC_FAKE)
static uint16_t FAKE_VALUE[SOC_ADC_PERIPH_NUM * 8];
void nht_adc_fake_set(int gpio, uint16_t v)
{
    if (gpio >= 0 && gpio < (int)(sizeof(FAKE_VALUE)/sizeof(FAKE_VALUE[0])))
        FAKE_VALUE[gpio] = v;
}
#endif

esp_err_t nht_adc_read_raw(nht_adc_t *a, uint16_t *raw)
{
    if (!a || !raw || !a->owns_unit) return ESP_ERR_INVALID_STATE;

#if defined(NHT_ADC_FAKE)
    /* Trình mô phỏng không có ngoại vi ADC: đọc thật sẽ quay vô hạn trong
       HAL chờ cờ chuyển đổi và làm nổ interrupt watchdog. */
    *raw = FAKE_VALUE[a->cfg.gpio];
    return ESP_OK;
#endif

    uint16_t buf[NHT_ADC_MAX_SAMPLES];
    uint8_t n = 0;
    for (uint8_t i = 0; i < a->cfg.samples; i++) {
        int v = 0;
        if (adc_oneshot_read(a->unit, a->channel, &v) == ESP_OK)
            buf[n++] = (uint16_t)(v < 0 ? 0 : v);
    }
    if (n == 0) return ESP_FAIL;

    *raw = median_u16(buf, n);
    return ESP_OK;
}

esp_err_t nht_adc_read_mv(nht_adc_t *a, int *mv)
{
    uint16_t raw;
    esp_err_t err = nht_adc_read_raw(a, &raw);
    if (err != ESP_OK) return err;

    if (a->calibrated) return adc_cali_raw_to_voltage(a->cali, raw, mv);

    /* Không hiệu chuẩn được: quy đổi thô theo dải danh định của mức suy hao. */
    int full_mv;
    switch (a->cfg.atten) {
    case ADC_ATTEN_DB_0:   full_mv = 950;  break;
    case ADC_ATTEN_DB_2_5: full_mv = 1250; break;
    case ADC_ATTEN_DB_6:   full_mv = 1750; break;
    default:               full_mv = 3100; break;
    }
    *mv = (int)((uint32_t)raw * full_mv / 4095u);
    return ESP_OK;
}

esp_err_t nht_adc_read_value(nht_adc_t *a, float *out)
{
    int mv;
    esp_err_t err = nht_adc_read_mv(a, &mv);
    if (err != ESP_OK) return err;
    *out = (mv / 1000.0f) * a->cfg.scale + a->cfg.offset;
    return ESP_OK;
}
