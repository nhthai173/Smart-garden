#include "nht_sched.h"

#define SECS_PER_DAY 86400

void nht_time_from_unix(int64_t unix_s, int32_t tz_offset_s, nht_time_t *out)
{
    int64_t local = unix_s + tz_offset_s;
    if (local < 0) local = 0;

    int64_t days = local / SECS_PER_DAY;
    int32_t rem  = (int32_t)(local - days * SECS_PER_DAY);

    out->day    = (uint32_t)days;
    out->hour   = (uint8_t)(rem / 3600);
    out->minute = (uint8_t)((rem % 3600) / 60);
    out->second = (uint8_t)(rem % 60);
    out->wday   = (uint8_t)((days + 4) % 7);  /* 1970-01-01 là thứ Năm */
}

void nht_sched_init(nht_sched_t *s, uint16_t catchup_window_s)
{
    memset(s, 0, sizeof(*s));
    s->catchup_window_s = catchup_window_s ? catchup_window_s
                                           : NHT_SCHED_DEFAULT_CATCHUP_S;
    s->next_id = 1;
}

void nht_sched_clear(nht_sched_t *s)
{
    uint16_t w = s->catchup_window_s;
    nht_sched_init(s, w);
}

static bool id_taken(const nht_sched_t *s, uint8_t id)
{
    for (int i = 0; i < NHT_SCHED_MAX_TASKS; i++)
        if (s->tasks[i].id == id) return true;
    return false;
}

static bool valid_time(const nht_sched_task_t *t)
{
    return t && t->hour <= 23 && t->minute <= 59;
}

bool nht_sched_add(nht_sched_t *s, const nht_sched_task_t *t, uint8_t *out_id)
{
    if (!valid_time(t)) return false;

    for (int i = 0; i < NHT_SCHED_MAX_TASKS; i++) {
        if (s->tasks[i].id != 0) continue;

        uint8_t id = s->next_id;
        for (int guard = 0; guard < 256 && (id == 0 || id_taken(s, id)); guard++)
            id++;
        if (id == 0 || id_taken(s, id)) return false;

        s->tasks[i] = *t;
        s->tasks[i].id = id;
        s->tasks[i].last_run_day = 0;
        s->next_id = (uint8_t)(id + 1);
        s->count++;
        if (out_id) *out_id = id;
        return true;
    }
    return false;  /* đầy */
}

nht_sched_task_t *nht_sched_find(nht_sched_t *s, uint8_t id)
{
    if (id == 0) return NULL;
    for (int i = 0; i < NHT_SCHED_MAX_TASKS; i++)
        if (s->tasks[i].id == id) return &s->tasks[i];
    return NULL;
}

bool nht_sched_update(nht_sched_t *s, const nht_sched_task_t *t)
{
    if (!valid_time(t)) return false;

    /* Con trỏ vào mảng thật. Cách hỏng kinh điển là lặp bằng bản sao theo
       value rồi sửa bản sao - hàm báo thành công mà dữ liệu không đổi. */
    nht_sched_task_t *dst = nht_sched_find(s, t->id);
    if (!dst) return false;

    bool time_changed = (dst->hour != t->hour) || (dst->minute != t->minute);
    uint32_t keep = dst->last_run_day;

    *dst = *t;
    dst->last_run_day = time_changed ? 0 : keep;
    return true;
}

bool nht_sched_remove(nht_sched_t *s, uint8_t id)
{
    nht_sched_task_t *t = nht_sched_find(s, id);
    if (!t) return false;
    memset(t, 0, sizeof(*t));
    if (s->count) s->count--;
    return true;
}

bool nht_sched_set_enabled(nht_sched_t *s, uint8_t id, bool enabled)
{
    nht_sched_task_t *t = nht_sched_find(s, id);
    if (!t) return false;
    t->enabled = enabled;
    return true;
}

/** Task này có được phép chạy vào `now->wday` không. */
static bool day_matches(const nht_sched_task_t *t, const nht_time_t *now)
{
    if (t->repeat_mask == NHT_SCHED_REPEAT_NONE) return true;  /* một lần */
    return (t->repeat_mask & (1u << now->wday)) != 0;
}

uint8_t nht_sched_due(nht_sched_t *s, const nht_time_t *now,
                      nht_sched_task_t *out)
{
    /* Chưa có giờ thật (SNTP chưa đồng bộ) thì không được quyết định gì.
       `day == 0` cũng chính là sentinel "chưa từng chạy" của last_run_day. */
    if (!now || now->day == 0) return 0;

    int32_t now_s = (int32_t)now->hour * 3600 + now->minute * 60 + now->second;

    for (int i = 0; i < NHT_SCHED_MAX_TASKS; i++) {
        nht_sched_task_t *t = &s->tasks[i];
        if (t->id == 0 || !t->enabled) continue;
        if (t->last_run_day == now->day) continue;
        if (!day_matches(t, now)) continue;

        int32_t delta = now_s - ((int32_t)t->hour * 3600 + t->minute * 60);

        /* delta < 0            : chưa tới giờ
           delta >= cửa sổ bù   : trễ quá, bỏ hôm nay (mai chạy tiếp) */
        if (delta < 0 || delta >= (int32_t)s->catchup_window_s) continue;

        t->last_run_day = now->day;
        uint8_t id = t->id;
        if (out) *out = *t;
        if (t->repeat_mask == NHT_SCHED_REPEAT_NONE) t->enabled = false;
        return id;
    }
    return 0;
}

uint32_t nht_sched_next_in(const nht_sched_t *s, const nht_time_t *now)
{
    if (!now) return UINT32_MAX;
    int32_t now_s = (int32_t)now->hour * 3600 + now->minute * 60 + now->second;
    uint32_t best = UINT32_MAX;

    for (int i = 0; i < NHT_SCHED_MAX_TASKS; i++) {
        const nht_sched_task_t *t = &s->tasks[i];
        if (t->id == 0 || !t->enabled) continue;

        int32_t task_s = (int32_t)t->hour * 3600 + t->minute * 60;

        /* Dò tối đa 8 ngày tới để bắt được cả mask thưa như "chỉ chủ nhật". */
        for (int d = 0; d < 8; d++) {
            nht_time_t probe = *now;
            probe.wday = (uint8_t)((now->wday + d) % 7);
            if (!day_matches(t, &probe)) continue;
            if (d == 0 && t->last_run_day == now->day) continue;

            int32_t delta = task_s - now_s + d * SECS_PER_DAY;
            if (delta < 0) continue;
            if ((uint32_t)delta < best) best = (uint32_t)delta;
            break;
        }
    }
    return best;
}
