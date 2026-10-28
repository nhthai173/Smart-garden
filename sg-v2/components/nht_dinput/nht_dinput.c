#include "nht_dinput.h"

void nht_dinput_init(nht_dinput_t *in, nht_din_state_t initial)
{
    if (!in->adc_max)     in->adc_max     = NHT_DIN_DEFAULT_ADC_MAX;
    if (!in->hold_on_ms)  in->hold_on_ms  = NHT_DIN_DEFAULT_HOLD_MS;
    if (!in->hold_off_ms) in->hold_off_ms = NHT_DIN_DEFAULT_HOLD_MS;

    /* Hai ngưỡng bằng nhau hoặc đảo nhau thì Schmitt mất tác dụng - tự nới
       ra ±12% quanh giá trị đã cho để vẫn còn vùng chết. */
    if (in->analog && in->thr_off >= in->thr_on) {
        uint32_t mid = in->thr_on ? in->thr_on : (in->adc_max / 2u);
        uint32_t d   = mid / 8u ? mid / 8u : 1u;
        in->thr_on  = (uint16_t)(mid + d);
        in->thr_off = (uint16_t)(mid > d ? mid - d : 0u);
    }

    in->state            = initial;
    in->pending          = initial;
    in->has_pending      = false;
    in->pending_since_ms = 0;
    in->changed_at_ms    = 0;
    in->transitions      = 0;
    in->last_raw         = 0;
}

/** Mức thô sau Schmitt, chưa tính trễ thời gian. */
static nht_din_state_t raw_level(const nht_dinput_t *in, uint16_t raw)
{
    if (!in->analog) {
        bool active = in->inverted ? (raw == 0) : (raw != 0);
        return active ? NHT_DIN_ACTIVE : NHT_DIN_INACTIVE;
    }

    uint16_t v = raw;
    if (in->inverted)
        v = (raw >= in->adc_max) ? 0u : (uint16_t)(in->adc_max - raw);

    /* Ngưỡng nào được áp phụ thuộc trạng thái hiện tại - đó chính là trễ. */
    if (in->state == NHT_DIN_ACTIVE)
        return (v <= in->thr_off) ? NHT_DIN_INACTIVE : NHT_DIN_ACTIVE;
    return (v >= in->thr_on) ? NHT_DIN_ACTIVE : NHT_DIN_INACTIVE;
}

bool nht_dinput_feed(nht_dinput_t *in, uint16_t raw, uint32_t now_ms)
{
    in->last_raw = raw;
    nht_din_state_t target = raw_level(in, raw);

    if (target == in->state) {
        in->has_pending = false;   /* gai nhiễu thoáng qua: quên luôn */
        return false;
    }

    if (!in->has_pending || in->pending != target) {
        in->pending          = target;
        in->pending_since_ms = now_ms;
        in->has_pending      = true;
        return false;
    }

    uint32_t need = (target == NHT_DIN_ACTIVE) ? in->hold_on_ms
                                               : in->hold_off_ms;
    if ((uint32_t)(now_ms - in->pending_since_ms) < need)
        return false;

    in->state         = target;
    in->has_pending   = false;
    in->changed_at_ms = now_ms;
    in->transitions++;

    if (in->on_change) in->on_change(in, in->state, in->user);
    return true;
}

uint32_t nht_dinput_stable_ms(const nht_dinput_t *in, uint32_t now_ms)
{
    return (uint32_t)(now_ms - in->changed_at_ms);
}

uint16_t nht_median_u16(uint16_t *buf, uint32_t n)
{
    if (!buf || n == 0) return 0;
    /* Insertion sort: n ở đây luôn nhỏ (3..9 mẫu). */
    for (uint32_t i = 1; i < n; i++) {
        uint16_t v = buf[i];
        uint32_t j = i;
        while (j > 0 && buf[j - 1] > v) { buf[j] = buf[j - 1]; j--; }
        buf[j] = v;
    }
    return buf[n / 2];
}
