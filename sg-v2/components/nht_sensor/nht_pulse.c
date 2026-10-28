#include "nht_pulse.h"
#include "soc/soc_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include <string.h>
#include <stdlib.h>

#if SOC_PCNT_SUPPORTED
#include "driver/pulse_cnt.h"
#else
#include "driver/gpio.h"
#endif

static const char *TAG = "nht_pulse";

/*
 * Hai backend, cùng một API.
 *
 *  SOC_PCNT_SUPPORTED (ESP32, S2, S3, C6, H2...): dùng bộ đếm phần cứng.
 *      Xung được đếm ngay cả khi CPU đang bận, không bỏ sót gì.
 *
 *  Không có PCNT (ESP32-C3, C2): đếm bằng ngắt GPIO. C3 không có peripheral
 *      PCNT - đây không phải là lựa chọn, mà là bắt buộc. Với lưu lượng kế
 *      thì vẫn thừa sức: YF-S201 ở 30 L/phút cho khoảng 225 Hz, còn cách rất
 *      xa giới hạn của ngắt GPIO. Lọc gai làm bằng phần mềm trong ISR.
 */

#define PCNT_HIGH_LIMIT 10000
#define PCNT_LOW_LIMIT     -1

struct nht_pulse_s {
    nht_pulse_cfg_t cfg;
    volatile uint64_t accumulated;
    uint64_t last_total;
    uint32_t last_ms;
    bool     have_last;

#if SOC_PCNT_SUPPORTED
    pcnt_unit_handle_t    unit;
    pcnt_channel_handle_t chan;
#else
    volatile int64_t last_edge_us;
    bool isr_added;
#endif
};

#if SOC_PCNT_SUPPORTED

static bool IRAM_ATTR on_reach(pcnt_unit_handle_t unit,
                               const pcnt_watch_event_data_t *edata, void *user)
{
    (void)unit;
    nht_pulse_t *p = (nht_pulse_t *)user;
    if (edata->watch_point_value == PCNT_HIGH_LIMIT)
        p->accumulated += PCNT_HIGH_LIMIT;
    return false;
}

static esp_err_t backend_init(nht_pulse_t *p)
{
    pcnt_unit_config_t ucfg = {
        .high_limit = PCNT_HIGH_LIMIT,
        .low_limit  = PCNT_LOW_LIMIT,
    };
    esp_err_t err = pcnt_new_unit(&ucfg, &p->unit);
    if (err != ESP_OK) return err;

    pcnt_glitch_filter_config_t fcfg = { .max_glitch_ns = p->cfg.glitch_ns };
    if ((err = pcnt_unit_set_glitch_filter(p->unit, &fcfg)) != ESP_OK) return err;

    pcnt_chan_config_t ccfg = {
        .edge_gpio_num  = p->cfg.gpio,
        .level_gpio_num = -1,
    };
    if ((err = pcnt_new_channel(p->unit, &ccfg, &p->chan)) != ESP_OK) return err;

    err = pcnt_channel_set_edge_action(p->chan,
                                       PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                       PCNT_CHANNEL_EDGE_ACTION_HOLD);
    if (err != ESP_OK) return err;

    if ((err = pcnt_unit_add_watch_point(p->unit, PCNT_HIGH_LIMIT)) != ESP_OK)
        return err;

    pcnt_event_callbacks_t cbs = { .on_reach = on_reach };
    if ((err = pcnt_unit_register_event_callbacks(p->unit, &cbs, p)) != ESP_OK)
        return err;

    if ((err = pcnt_unit_enable(p->unit))      != ESP_OK) return err;
    if ((err = pcnt_unit_clear_count(p->unit)) != ESP_OK) return err;
    return pcnt_unit_start(p->unit);
}

static void backend_deinit(nht_pulse_t *p)
{
    if (!p->unit) return;
    pcnt_unit_stop(p->unit);
    pcnt_unit_disable(p->unit);
    if (p->chan) pcnt_del_channel(p->chan);
    pcnt_del_unit(p->unit);
    p->unit = NULL;
    p->chan = NULL;
}

static uint64_t backend_total(nht_pulse_t *p)
{
    int count = 0;
    pcnt_unit_get_count(p->unit, &count);
    if (count < 0) count = 0;
    return p->accumulated + (uint64_t)count;
}

static void backend_clear(nht_pulse_t *p)
{
    pcnt_unit_clear_count(p->unit);
    p->accumulated = 0;
}

static const char *backend_name(void) { return "PCNT phần cứng"; }

#else  /* ---- không có PCNT: đếm bằng ngắt GPIO ---- */

static void IRAM_ATTR pulse_isr(void *arg)
{
    nht_pulse_t *p = (nht_pulse_t *)arg;
    int64_t now = esp_timer_get_time();

    /* Lọc gai: bỏ qua sườn đến sớm hơn glitch_ns kể từ sườn trước. */
    if (now - p->last_edge_us < (int64_t)p->cfg.glitch_ns / 1000) return;

    p->last_edge_us = now;
    p->accumulated++;
}

static esp_err_t backend_init(nht_pulse_t *p)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << p->cfg.gpio,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = p->cfg.pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_POSEDGE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;

    /* Dịch vụ ISR dùng chung toàn hệ - đã cài rồi thì không phải lỗi. */
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    err = gpio_isr_handler_add(p->cfg.gpio, pulse_isr, p);
    if (err != ESP_OK) return err;

    p->isr_added = true;
    return ESP_OK;
}

static void backend_deinit(nht_pulse_t *p)
{
    if (p->isr_added) {
        gpio_isr_handler_remove(p->cfg.gpio);
        p->isr_added = false;
    }
}

static uint64_t backend_total(nht_pulse_t *p) { return p->accumulated; }
static void backend_clear(nht_pulse_t *p)     { p->accumulated = 0; }
static const char *backend_name(void)         { return "ngắt GPIO"; }

#endif

esp_err_t nht_pulse_init(nht_pulse_t **out, const nht_pulse_cfg_t *cfg)
{
    if (!out || !cfg) return ESP_ERR_INVALID_ARG;

    nht_pulse_t *p = calloc(1, sizeof(*p));
    if (!p) return ESP_ERR_NO_MEM;

    p->cfg = *cfg;
    if (p->cfg.pulses_per_unit <= 0.0f) p->cfg.pulses_per_unit = 1.0f;
    if (p->cfg.glitch_ns == 0)          p->cfg.glitch_ns = 1000;

    esp_err_t err = backend_init(p);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "khởi tạo hỏng: %s", esp_err_to_name(err));
        nht_pulse_deinit(p);
        return err;
    }

    ESP_LOGI(TAG, "đếm xung GPIO%d qua %s, %.1f xung/đơn vị",
             cfg->gpio, backend_name(), p->cfg.pulses_per_unit);
    *out = p;
    return ESP_OK;
}

void nht_pulse_deinit(nht_pulse_t *p)
{
    if (!p) return;
    backend_deinit(p);
    free(p);
}

uint64_t nht_pulse_total(nht_pulse_t *p)
{
    return p ? backend_total(p) : 0;
}

float nht_pulse_volume(nht_pulse_t *p)
{
    if (!p) return 0.0f;
    return (float)nht_pulse_total(p) / p->cfg.pulses_per_unit;
}

float nht_pulse_rate(nht_pulse_t *p, uint32_t now_ms)
{
    if (!p) return 0.0f;
    uint64_t total = nht_pulse_total(p);

    if (!p->have_last) {
        p->have_last  = true;
        p->last_total = total;
        p->last_ms    = now_ms;
        return 0.0f;
    }

    uint32_t dt = now_ms - p->last_ms;
    if (dt == 0) return 0.0f;

    uint64_t d = total - p->last_total;
    p->last_total = total;
    p->last_ms    = now_ms;

    return ((float)d / p->cfg.pulses_per_unit) * 60000.0f / (float)dt;
}

void nht_pulse_reset(nht_pulse_t *p)
{
    if (!p) return;
    backend_clear(p);
    p->last_total = 0;
    p->have_last  = false;
}
