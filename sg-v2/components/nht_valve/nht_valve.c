#include "nht_valve.h"
#include <string.h>

static void emit(nht_valve_t *v, nht_valve_event_t ev)
{
    if (v->on_event) v->on_event(v, ev, v->evt_user);
}

static bool has(int pin) { return pin >= 0; }

/* Bật nguồn AC tổng nếu có; nó tự cắt sau ac_auto_off_ms KỂ TỪ LẦN CHUYỂN
   ĐỘNG CUỐI. Tính từ lần đầu thì lệnh đóng ngay sau lệnh mở có thể bị cắt
   điện giữa hành trình và van đứng lại hé mở. */
static void ac_wake(nht_valve_t *v, uint32_t now_ms)
{
    if (has(v->cfg.pin_ac)) nht_act_retrigger(&v->ac, now_ms);
}

/** Chọn chiều rồi cấp điện cho động cơ quay `ms`. */
static void do_motion(nht_valve_t *v, bool close, uint32_t ms, uint32_t now_ms)
{
    if (has(v->cfg.pin_dir)) {
        if (close) nht_act_on(&v->dir, now_ms);
        else       nht_act_off_force(&v->dir, now_ms);
    }
    ac_wake(v, now_ms);
    nht_act_off_force(&v->power, now_ms);     /* đặt lại hạn từ đầu */
    nht_act_on_for(&v->power, ms, now_ms);
    v->travel_until_ms = now_ms + ms;
    v->travelling      = true;
}

/**
 * Quay động cơ theo chiều mới. Đang quay dở (đảo chiều giữa hành trình) thì
 * ngắt điện, nghỉ `reverse_settle_ms` rồi mới đổi chiều: không bao giờ để
 * relay chiều chuyển mạch khi động cơ còn đang có điện. Hành trình mới luôn
 * tính đủ từ đầu, nên van chắc chắn tới hết cữ dù đã quay được bao xa.
 */
static void start_motion(nht_valve_t *v, bool close, uint32_t ms,
                         uint32_t now_ms)
{
    if (v->travelling || nht_act_is_on(&v->power)) {
        nht_act_off_force(&v->power, now_ms);
        v->travelling    = false;
        v->move_pending  = true;
        v->move_close    = close;
        v->move_ms       = ms;
        v->move_at_ms    = now_ms + v->cfg.reverse_settle_ms;
        return;
    }
    v->move_pending = false;
    do_motion(v, close, ms, now_ms);
}

static void begin_open(nht_valve_t *v, uint32_t now_ms)
{
    if (v->cfg.type == NHT_VALVE_SOLENOID) {
        ac_wake(v, now_ms);
        nht_act_on(&v->power, now_ms);           /* cuộn van giữ điện */
        if (has(v->cfg.pin_pump)) nht_act_on(&v->pump, now_ms);
        v->state = NHT_VALVE_OPEN;
        emit(v, NHT_VALVE_EV_OPENED);
        return;
    }

    uint32_t pct = v->level_pct ? v->level_pct : 100u;
    if (pct > 100u) pct = 100u;
    uint32_t ms = v->cfg.travel_ms * pct / 100u;
    if (ms == 0) ms = v->cfg.travel_ms;

    start_motion(v, false, ms, now_ms);
    v->state = NHT_VALVE_OPENING;

    /* Bơm có on_delay riêng nên bật ngay ở đây là đúng: nó tự đợi. */
    if (has(v->cfg.pin_pump)) nht_act_on(&v->pump, now_ms);
}

static void begin_close(nht_valve_t *v, uint32_t now_ms)
{
    /* Bơm tắt TRƯỚC khi van khép lại - không dồn áp vào đường ống kín. */
    if (has(v->cfg.pin_pump)) nht_act_off_force(&v->pump, now_ms);

    if (v->cfg.type == NHT_VALVE_SOLENOID) {
        nht_act_off_force(&v->power, now_ms);
        v->state = NHT_VALVE_CLOSED;
        return;
    }

    start_motion(v, true, v->cfg.travel_ms, now_ms);
    v->state = NHT_VALVE_CLOSING;
}

/** Sự kiện của đầu ra ảo `run` - nơi giữ thời lượng tưới. */
static void on_run_event(nht_act_t *a, nht_act_event_t ev, void *user)
{
    (void)a;
    nht_valve_t *v = (nht_valve_t *)user;
    uint32_t now = v->now_ms;

    switch (ev) {
    case NHT_ACT_EV_ON:
        begin_open(v, now);
        break;
    case NHT_ACT_EV_OFF:
        begin_close(v, now);
        emit(v, NHT_VALVE_EV_CLOSED);
        break;
    case NHT_ACT_EV_AUTO_OFF:
        begin_close(v, now);
        emit(v, NHT_VALVE_EV_AUTO_CLOSED);
        break;
    case NHT_ACT_EV_INHIBITED:
        begin_close(v, now);
        emit(v, NHT_VALVE_EV_INHIBITED);
        break;
    }
}

static void mk_act(nht_act_t *a, const nht_valve_cfg_t *cfg, int pin,
                   uint32_t auto_off_ms, uint32_t on_delay_ms,
                   const char *name, uint32_t now_ms)
{
    nht_act_cfg_t c;
    memset(&c, 0, sizeof(c));
    c.pin         = pin;
    c.active_high = cfg->active_high;
    c.auto_off_ms = auto_off_ms;
    c.on_delay_ms = on_delay_ms;
    c.write       = cfg->write;
    c.user        = cfg->user;
    c.name        = name;
    nht_act_init(a, &c, now_ms);
}

nht_valve_err_t nht_valve_init(nht_valve_t *v, const nht_valve_cfg_t *cfg,
                               uint32_t now_ms)
{
    if (!v || !cfg) return NHT_VALVE_ERR_PARAM;

    memset(v, 0, sizeof(*v));
    v->cfg = *cfg;
    if (!v->cfg.default_run_ms) v->cfg.default_run_ms = 60000u;
    if (!v->cfg.max_run_ms)     v->cfg.max_run_ms     = 1800000u;  /* 30 phút */
    if (v->cfg.type == NHT_VALVE_BALL && !v->cfg.travel_ms)
        v->cfg.travel_ms = 8000u;
    if (!v->cfg.reverse_settle_ms) v->cfg.reverse_settle_ms = 100u;
    /* AC phải sống đủ lâu để hết một hành trình, kể cả lúc đảo chiều. */
    if (v->cfg.ac_auto_off_ms) {
        uint32_t min_ac = v->cfg.travel_ms + 2u * v->cfg.reverse_settle_ms + 2000u;
        if (v->cfg.ac_auto_off_ms < min_ac) v->cfg.ac_auto_off_ms = min_ac;
    }

    mk_act(&v->power, &v->cfg, v->cfg.pin_power,
           v->cfg.type == NHT_VALVE_BALL ? v->cfg.travel_ms : 0, 0,
           "power", now_ms);
    mk_act(&v->dir,   &v->cfg, v->cfg.pin_dir,  0, 0, "dir",  now_ms);
    mk_act(&v->pump,  &v->cfg, v->cfg.pin_pump, 0, v->cfg.pump_delay_ms,
           "pump", now_ms);
    mk_act(&v->ac,    &v->cfg, v->cfg.pin_ac,   v->cfg.ac_auto_off_ms, 0,
           "ac", now_ms);

    /* Đầu ra ảo: không có chân, chỉ giữ thời lượng và mốc tự tắt. */
    nht_act_cfg_t rc;
    memset(&rc, 0, sizeof(rc));
    rc.pin         = -1;
    rc.active_high = true;
    rc.auto_off_ms = v->cfg.default_run_ms;
    rc.name        = "run";
    nht_act_init(&v->run, &rc, now_ms);
    nht_act_on_event(&v->run, on_run_event, v);
    v->now_ms = now_ms;

    /* Đưa van về ĐÓNG chắc chắn. Với van bi phải quay đủ hành trình: sau khi
       thiết bị tự reset giữa lúc đang tưới, không ai biết van đang ở đâu. */
    v->state = NHT_VALVE_CLOSED;
    if (v->cfg.type == NHT_VALVE_BALL) {
        begin_close(v, now_ms);
    } else {
        nht_act_off_force(&v->power, now_ms);
    }
    return NHT_VALVE_OK;
}

void nht_valve_on_event(nht_valve_t *v, nht_valve_evt_cb_t cb, void *user)
{
    v->on_event = cb;
    v->evt_user = user;
}

nht_valve_err_t nht_valve_open(nht_valve_t *v, uint32_t run_ms,
                               uint8_t level_pct, uint32_t now_ms)
{
    if (!v) return NHT_VALVE_ERR_PARAM;
    v->now_ms = now_ms;
    if (v->inhibit) return NHT_VALVE_ERR_INHIBIT;

    uint32_t ms = run_ms ? run_ms : v->cfg.default_run_ms;
    if (ms > v->cfg.max_run_ms) ms = v->cfg.max_run_ms;   /* trần cứng */

    /* Đang quay để ĐÓNG thì phải đợi xong - đảo chiều giữa hành trình làm
       van đứng ở vị trí không xác định. */
    if (v->state == NHT_VALVE_CLOSING) return NHT_VALVE_ERR_BUSY;

    if (level_pct) v->level_pct = level_pct > 100 ? 100 : level_pct;
    else if (!nht_act_is_on(&v->run)) v->level_pct = 100;

    /* Đang mở: nht_act_on_for chỉ đặt lại hạn, không kích lại trình tự mở. */
    return nht_act_on_for(&v->run, ms, now_ms) == NHT_ACT_OK
               ? NHT_VALVE_OK : NHT_VALVE_ERR_INHIBIT;
}

nht_valve_err_t nht_valve_close(nht_valve_t *v, uint32_t now_ms)
{
    if (!v) return NHT_VALVE_ERR_PARAM;
    v->now_ms = now_ms;
    if (!nht_act_is_on(&v->run) && v->state == NHT_VALVE_CLOSED)
        return NHT_VALVE_OK;
    nht_act_off_force(&v->run, now_ms);
    return NHT_VALVE_OK;
}

nht_valve_err_t nht_valve_close_force(nht_valve_t *v, uint32_t now_ms)
{
    if (!v) return NHT_VALVE_ERR_PARAM;
    v->now_ms = now_ms;
    nht_act_off_force(&v->run, now_ms);
    if (v->state != NHT_VALVE_CLOSING && v->state != NHT_VALVE_CLOSED)
        begin_close(v, now_ms);
    return NHT_VALVE_OK;
}

void nht_valve_set_inhibit(nht_valve_t *v, bool inhibit, uint32_t now_ms)
{
    if (!v) return;
    v->now_ms  = now_ms;
    v->inhibit = inhibit;
    nht_act_set_inhibit(&v->run, inhibit, now_ms);
}

void nht_valve_tick(nht_valve_t *v, uint32_t now_ms)
{
    if (!v) return;
    v->now_ms = now_ms;

    nht_act_tick(&v->run, now_ms);
    nht_act_tick(&v->power, now_ms);
    nht_act_tick(&v->dir, now_ms);
    nht_act_tick(&v->pump, now_ms);
    nht_act_tick(&v->ac, now_ms);

    /* Cuộn van điện từ nhận điện qua AC: đang mở thì AC không được tự cắt. */
    if (v->cfg.type == NHT_VALVE_SOLENOID && v->state == NHT_VALVE_OPEN &&
        has(v->cfg.pin_ac))
        nht_act_retrigger(&v->ac, now_ms);

    if (v->move_pending && (int32_t)(now_ms - v->move_at_ms) >= 0) {
        v->move_pending = false;
        do_motion(v, v->move_close, v->move_ms, now_ms);
    }

    if (v->travelling && (int32_t)(now_ms - v->travel_until_ms) >= 0) {
        v->travelling = false;

        if (v->state == NHT_VALVE_OPENING) {
            v->state = NHT_VALVE_OPEN;
            emit(v, NHT_VALVE_EV_OPENED);
        } else if (v->state == NHT_VALVE_CLOSING) {
            v->state = NHT_VALVE_CLOSED;
            /* Nhả chân chiều để cuộn relay không giữ điện vô ích. */
            if (has(v->cfg.pin_dir)) nht_act_off_force(&v->dir, now_ms);
        }
    }
}

bool nht_valve_next_wake_ms(const nht_valve_t *v, uint32_t now_ms,
                            uint32_t *out_ms)
{
    if (!v || !out_ms) return false;

    bool any = false;
    uint32_t best = 0, ms;
    const nht_act_t *acts[] = { &v->run, &v->power, &v->dir, &v->pump, &v->ac };

    for (unsigned i = 0; i < sizeof(acts) / sizeof(acts[0]); i++) {
        if (nht_act_next_wake_ms(acts[i], now_ms, &ms) && (!any || ms < best)) {
            best = ms; any = true;
        }
    }
    if (v->travelling) {
        int32_t left = (int32_t)(v->travel_until_ms - now_ms);
        ms = left > 0 ? (uint32_t)left : 0;
        if (!any || ms < best) { best = ms; any = true; }
    }
    if (v->move_pending) {
        int32_t left = (int32_t)(v->move_at_ms - now_ms);
        ms = left > 0 ? (uint32_t)left : 0;
        if (!any || ms < best) { best = ms; any = true; }
    }
    if (any) *out_ms = best;
    return any;
}

uint32_t nht_valve_remaining_ms(const nht_valve_t *v, uint32_t now_ms)
{
    return nht_act_remaining_ms(&v->run, now_ms);
}

const char *nht_valve_state_str(nht_valve_state_t s)
{
    switch (s) {
    case NHT_VALVE_CLOSED:  return "closed";
    case NHT_VALVE_OPENING: return "opening";
    case NHT_VALVE_OPEN:    return "open";
    case NHT_VALVE_CLOSING: return "closing";
    default:                return "?";
    }
}
