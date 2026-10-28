#include "nht_actuator.h"
#include <string.h>

#if !defined(NHT_HOST_TEST)
#include "driver/gpio.h"
#endif

static void hw_write(nht_act_t *a, bool logical_on)
{
    bool level = a->cfg.active_high ? logical_on : !logical_on;

    if (a->cfg.write) {
        a->cfg.write(a->cfg.pin, level, a->cfg.user);
        return;
    }
#if !defined(NHT_HOST_TEST)
    if (a->cfg.pin >= 0)
        gpio_set_level((gpio_num_t)a->cfg.pin, level ? 1 : 0);
#endif
}

static void emit(nht_act_t *a, nht_act_event_t ev)
{
    if (a->on_event) a->on_event(a, ev, a->evt_user);
}

static void go_on(nht_act_t *a, uint32_t now_ms)
{
    a->state          = NHT_ACT_ON;
    a->state_since_ms = now_ms;
    a->on_at_ms       = now_ms;

    uint32_t dur = a->once_ms ? a->once_ms : a->cfg.auto_off_ms;
    a->has_deadline = (dur != 0);
    a->deadline_ms  = now_ms + dur;

    hw_write(a, true);
    emit(a, NHT_ACT_EV_ON);
}

static void go_off(nht_act_t *a, uint32_t now_ms, nht_act_event_t ev)
{
    bool was_on = (a->state != NHT_ACT_OFF);
    a->state          = NHT_ACT_OFF;
    a->state_since_ms = now_ms;
    a->once_ms        = 0;
    a->has_deadline   = false;
    a->deadline_ms    = 0;

    hw_write(a, false);
    if (was_on) emit(a, ev);
}

nht_act_err_t nht_act_init(nht_act_t *a, const nht_act_cfg_t *cfg,
                           uint32_t now_ms)
{
    if (!a || !cfg) return NHT_ACT_ERR_PARAM;

    memset(a, 0, sizeof(*a));
    a->cfg = *cfg;

#if !defined(NHT_HOST_TEST)
    if (a->cfg.pin >= 0 && !a->cfg.write) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << a->cfg.pin,
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);
    }
#endif

    a->state          = NHT_ACT_OFF;
    a->state_since_ms = now_ms;
    hw_write(a, false);

    if (cfg->startup == NHT_ACT_START_ON)
        go_on(a, now_ms);

    return NHT_ACT_OK;
}

void nht_act_on_event(nht_act_t *a, nht_act_evt_cb_t cb, void *user)
{
    a->on_event = cb;
    a->evt_user = user;
}

nht_act_err_t nht_act_on_for(nht_act_t *a, uint32_t ms, uint32_t now_ms)
{
    if (!a) return NHT_ACT_ERR_PARAM;
    if (a->inhibit) return NHT_ACT_ERR_INHIBIT;

    uint32_t dur = ms ? ms : a->cfg.auto_off_ms;

    /* ĐANG BẬT (hoặc đang chờ bật): chỉ đặt lại hạn.
       Không chạy lại trình tự bật, không dời `on_at_ms`. Nhờ vậy một lệnh
       trùng lặp - ví dụ event đồng bộ dội ngược về - không rút ngắn được
       lượt đang chạy. */
    if (a->state == NHT_ACT_ON) {
        a->once_ms      = dur;
        a->has_deadline = (dur != 0);
        a->deadline_ms  = a->on_at_ms + dur;
        return NHT_ACT_OK;
    }
    if (a->state == NHT_ACT_PENDING_ON) {
        a->once_ms = dur;
        return NHT_ACT_OK;
    }

    if (a->cfg.min_off_ms &&
        (uint32_t)(now_ms - a->state_since_ms) < a->cfg.min_off_ms)
        return NHT_ACT_ERR_MIN_OFF;

    a->once_ms   = dur;
    a->cmd_at_ms = now_ms;

    if (a->cfg.on_delay_ms) {
        a->state          = NHT_ACT_PENDING_ON;
        a->state_since_ms = now_ms;
    } else {
        go_on(a, now_ms);
    }
    return NHT_ACT_OK;
}

nht_act_err_t nht_act_on(nht_act_t *a, uint32_t now_ms)
{
    /* Đang bật thì giữ nguyên hạn hiện có - kể cả hạn do on_for() đặt. */
    if (a && (a->state == NHT_ACT_ON || a->state == NHT_ACT_PENDING_ON))
        return a->inhibit ? NHT_ACT_ERR_INHIBIT : NHT_ACT_OK;
    return nht_act_on_for(a, 0, now_ms);
}

nht_act_err_t nht_act_retrigger(nht_act_t *a, uint32_t now_ms)
{
    if (!a) return NHT_ACT_ERR_PARAM;
    if (a->state != NHT_ACT_ON) return nht_act_on(a, now_ms);

    uint32_t dur = a->once_ms ? a->once_ms : a->cfg.auto_off_ms;
    if (dur) {
        a->has_deadline = true;
        a->deadline_ms  = now_ms + dur;
    }
    return NHT_ACT_OK;
}

nht_act_err_t nht_act_extend(nht_act_t *a, uint32_t ms, uint32_t now_ms)
{
    if (!a) return NHT_ACT_ERR_PARAM;
    if (a->state != NHT_ACT_ON) return nht_act_on_for(a, ms, now_ms);

    uint32_t base = a->has_deadline ? a->deadline_ms : now_ms;
    if ((int32_t)(base - now_ms) < 0) base = now_ms;
    a->deadline_ms  = base + ms;
    a->has_deadline = true;
    a->once_ms      = (uint32_t)(a->deadline_ms - a->on_at_ms);
    return NHT_ACT_OK;
}

nht_act_err_t nht_act_off(nht_act_t *a, uint32_t now_ms)
{
    if (!a) return NHT_ACT_ERR_PARAM;
    if (a->state == NHT_ACT_OFF) return NHT_ACT_OK;

    if (a->state == NHT_ACT_ON && a->cfg.min_on_ms &&
        (uint32_t)(now_ms - a->on_at_ms) < a->cfg.min_on_ms)
        return NHT_ACT_ERR_MIN_ON;

    go_off(a, now_ms, NHT_ACT_EV_OFF);
    return NHT_ACT_OK;
}

nht_act_err_t nht_act_off_force(nht_act_t *a, uint32_t now_ms)
{
    if (!a) return NHT_ACT_ERR_PARAM;
    go_off(a, now_ms, NHT_ACT_EV_OFF);
    return NHT_ACT_OK;
}

nht_act_err_t nht_act_set(nht_act_t *a, bool on, uint32_t now_ms)
{
    return on ? nht_act_on(a, now_ms) : nht_act_off(a, now_ms);
}

nht_act_err_t nht_act_sync(nht_act_t *a, bool on, uint32_t now_ms)
{
    if (!a) return NHT_ACT_ERR_PARAM;
    /* Chốt chặn: trạng thái mong muốn trùng trạng thái hiện tại -> không
       làm gì. Thiếu đúng dòng này là sinh ra vòng lặp tự dội. */
    if (on == (a->state != NHT_ACT_OFF)) return NHT_ACT_OK;
    return nht_act_set(a, on, now_ms);
}

void nht_act_set_inhibit(nht_act_t *a, bool inhibit, uint32_t now_ms)
{
    if (!a) return;
    a->inhibit = inhibit;
    if (inhibit && a->state != NHT_ACT_OFF)
        go_off(a, now_ms, NHT_ACT_EV_INHIBITED);
}

void nht_act_tick(nht_act_t *a, uint32_t now_ms)
{
    if (!a) return;

    if (a->state == NHT_ACT_PENDING_ON) {
        if ((uint32_t)(now_ms - a->cmd_at_ms) >= a->cfg.on_delay_ms)
            go_on(a, now_ms);
        return;
    }

    if (a->state == NHT_ACT_ON && a->has_deadline &&
        (int32_t)(now_ms - a->deadline_ms) >= 0)
        go_off(a, now_ms, NHT_ACT_EV_AUTO_OFF);
}

bool nht_act_next_wake_ms(const nht_act_t *a, uint32_t now_ms, uint32_t *out_ms)
{
    if (!a || !out_ms) return false;

    int32_t left;
    if (a->state == NHT_ACT_PENDING_ON)
        left = (int32_t)(a->cmd_at_ms + a->cfg.on_delay_ms - now_ms);
    else if (a->state == NHT_ACT_ON && a->has_deadline)
        left = (int32_t)(a->deadline_ms - now_ms);
    else
        return false;

    *out_ms = left > 0 ? (uint32_t)left : 0;
    return true;
}

uint32_t nht_act_remaining_ms(const nht_act_t *a, uint32_t now_ms)
{
    if (!a || a->state != NHT_ACT_ON || !a->has_deadline) return 0;
    int32_t left = (int32_t)(a->deadline_ms - now_ms);
    return left > 0 ? (uint32_t)left : 0;
}

const char *nht_act_state_str(nht_act_state_t s)
{
    switch (s) {
    case NHT_ACT_OFF:        return "off";
    case NHT_ACT_PENDING_ON: return "pending";
    case NHT_ACT_ON:         return "on";
    default:                 return "?";
    }
}
