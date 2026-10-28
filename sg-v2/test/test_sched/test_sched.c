/**
 * Hồi quy cho nht_sched.
 * Hai test quan trọng nhất tái hiện đúng hai lỗi của bản Arduino trước:
 *   - test_catchup_*  : lịch bị bỏ khi vòng lặp trễ quá một phút
 *   - test_update_*   : update() báo thành công mà không sửa gì
 */
#include <unity.h>
#include <string.h>
#include "nht_sched.h"

typedef struct { uint16_t duration_s; uint8_t level; } payload_t;

static nht_sched_t S;

/* 2026-08-26 là thứ Tư. day = 20691, wday = 3. */
#define DAY_WED 20691u

static nht_time_t at(uint32_t day, uint8_t wday, uint8_t h, uint8_t m, uint8_t s)
{
    nht_time_t t = { .day = day, .wday = wday, .hour = h, .minute = m, .second = s };
    return t;
}

static uint8_t add_task(uint8_t h, uint8_t m, uint8_t mask, uint16_t dur)
{
    nht_sched_task_t t;
    memset(&t, 0, sizeof(t));
    t.hour = h; t.minute = m; t.repeat_mask = mask; t.enabled = true;
    payload_t p = { .duration_s = dur, .level = 100 };
    nht_sched_set_payload(&t, &p, sizeof(p));

    uint8_t id = 0;
    TEST_ASSERT_TRUE(nht_sched_add(&S, &t, &id));
    return id;
}

void setUp(void)    { nht_sched_init(&S, NHT_SCHED_DEFAULT_CATCHUP_S); }
void tearDown(void) { }

/* --- cơ bản --- */

static void test_due_at_exact_time(void)
{
    add_task(6, 0, NHT_SCHED_REPEAT_DAILY, 900);
    nht_time_t now = at(DAY_WED, 3, 6, 0, 0);

    nht_sched_task_t out;
    TEST_ASSERT_NOT_EQUAL(0, nht_sched_due(&S, &now, &out));

    payload_t p;
    memcpy(&p, nht_sched_payload(&out), sizeof(p));
    TEST_ASSERT_EQUAL_UINT16(900, p.duration_s);
}

static void test_not_due_before_time(void)
{
    add_task(6, 0, NHT_SCHED_REPEAT_DAILY, 900);
    nht_time_t now = at(DAY_WED, 3, 5, 59, 59);
    TEST_ASSERT_EQUAL(0, nht_sched_due(&S, &now, NULL));
}

static void test_no_time_no_run(void)
{
    /* Chưa đồng bộ SNTP (day == 0) thì tuyệt đối không được kích lịch. */
    add_task(6, 0, NHT_SCHED_REPEAT_DAILY, 900);
    nht_time_t now = at(0, 3, 6, 0, 0);
    TEST_ASSERT_EQUAL(0, nht_sched_due(&S, &now, NULL));
}

/* --- LỖI v1 #1: bỏ lịch khi tick bị trễ --- */

static void test_catchup_runs_when_tick_is_late(void)
{
    add_task(6, 0, NHT_SCHED_REPEAT_DAILY, 900);

    /* Thiết bị bận OTA / TLS handshake, mãi 6h03 mới quay lại kiểm tra.
       Cách khớp đúng-phút của v1 sẽ bỏ luôn lịch hôm nay. */
    nht_time_t late = at(DAY_WED, 3, 6, 3, 0);
    TEST_ASSERT_NOT_EQUAL(0, nht_sched_due(&S, &late, NULL));
}

static void test_catchup_gives_up_after_window(void)
{
    add_task(6, 0, NHT_SCHED_REPEAT_DAILY, 900);
    /* Trễ 11 phút, quá cửa sổ 10 phút -> bỏ hôm nay, không tưới lúc trưa. */
    nht_time_t too_late = at(DAY_WED, 3, 6, 11, 0);
    TEST_ASSERT_EQUAL(0, nht_sched_due(&S, &too_late, NULL));
}

static void test_runs_only_once_per_day(void)
{
    add_task(6, 0, NHT_SCHED_REPEAT_DAILY, 900);

    nht_time_t t1 = at(DAY_WED, 3, 6, 0, 0);
    TEST_ASSERT_NOT_EQUAL(0, nht_sched_due(&S, &t1, NULL));

    /* Mọi tick còn lại trong cửa sổ bù không được kích lại. */
    for (int s = 1; s < 600; s += 7) {
        nht_time_t t = at(DAY_WED, 3, 6, (uint8_t)(s / 60), (uint8_t)(s % 60));
        TEST_ASSERT_EQUAL(0, nht_sched_due(&S, &t, NULL));
    }

    /* Hôm sau thì chạy tiếp. */
    nht_time_t tomorrow = at(DAY_WED + 1, 4, 6, 0, 0);
    TEST_ASSERT_NOT_EQUAL(0, nht_sched_due(&S, &tomorrow, NULL));
}

/* --- LỖI v1 #2: update() không sửa được gì --- */

static void test_update_actually_persists(void)
{
    uint8_t id = add_task(6, 0, NHT_SCHED_REPEAT_DAILY, 900);

    nht_sched_task_t t = *nht_sched_find(&S, id);
    payload_t p = { .duration_s = 1800, .level = 50 };
    nht_sched_set_payload(&t, &p, sizeof(p));
    t.minute = 30;

    TEST_ASSERT_TRUE(nht_sched_update(&S, &t));

    /* Đọc lại TỪ MẢNG THẬT, không phải từ bản sao vừa truyền vào. */
    const nht_sched_task_t *back = nht_sched_find(&S, id);
    TEST_ASSERT_EQUAL_UINT8(30, back->minute);

    payload_t q;
    memcpy(&q, back->payload, sizeof(q));
    TEST_ASSERT_EQUAL_UINT16(1800, q.duration_s);
    TEST_ASSERT_EQUAL_UINT8(50, q.level);
}

static void test_update_unknown_id_fails(void)
{
    add_task(6, 0, NHT_SCHED_REPEAT_DAILY, 900);
    nht_sched_task_t t;
    memset(&t, 0, sizeof(t));
    t.id = 99; t.hour = 7; t.enabled = true;
    TEST_ASSERT_FALSE(nht_sched_update(&S, &t));
}

static void test_update_time_allows_rerun_today(void)
{
    uint8_t id = add_task(6, 0, NHT_SCHED_REPEAT_DAILY, 900);
    nht_time_t morning = at(DAY_WED, 3, 6, 0, 0);
    TEST_ASSERT_NOT_EQUAL(0, nht_sched_due(&S, &morning, NULL));

    /* Đổi sang 18h00 thì chiều nay phải chạy được, dù sáng đã chạy rồi. */
    nht_sched_task_t t = *nht_sched_find(&S, id);
    t.hour = 18; t.minute = 0;
    TEST_ASSERT_TRUE(nht_sched_update(&S, &t));

    nht_time_t evening = at(DAY_WED, 3, 18, 0, 0);
    TEST_ASSERT_NOT_EQUAL(0, nht_sched_due(&S, &evening, NULL));
}

/* --- lặp theo thứ --- */

static void test_weekday_mask(void)
{
    add_task(6, 0, NHT_SCHED_REPEAT_WEEKEND, 900);   /* CN + T7 */

    nht_time_t wed = at(DAY_WED, 3, 6, 0, 0);
    TEST_ASSERT_EQUAL(0, nht_sched_due(&S, &wed, NULL));

    nht_time_t sat = at(DAY_WED + 3, 6, 6, 0, 0);
    TEST_ASSERT_NOT_EQUAL(0, nht_sched_due(&S, &sat, NULL));
}

static void test_one_shot_disables_itself(void)
{
    uint8_t id = add_task(6, 0, NHT_SCHED_REPEAT_NONE, 900);
    nht_time_t now = at(DAY_WED, 3, 6, 0, 0);
    TEST_ASSERT_NOT_EQUAL(0, nht_sched_due(&S, &now, NULL));
    TEST_ASSERT_FALSE(nht_sched_find(&S, id)->enabled);
}

/* --- CRUD --- */

static void test_add_remove_ids_unique(void)
{
    uint8_t a = add_task(6, 0, NHT_SCHED_REPEAT_DAILY, 900);
    uint8_t b = add_task(7, 0, NHT_SCHED_REPEAT_DAILY, 900);
    TEST_ASSERT_NOT_EQUAL(a, b);
    TEST_ASSERT_EQUAL_UINT8(2, S.count);

    TEST_ASSERT_TRUE(nht_sched_remove(&S, a));
    TEST_ASSERT_NULL(nht_sched_find(&S, a));
    TEST_ASSERT_EQUAL_UINT8(1, S.count);
    TEST_ASSERT_FALSE(nht_sched_remove(&S, a));
}

static void test_reject_invalid_time(void)
{
    nht_sched_task_t t;
    memset(&t, 0, sizeof(t));
    t.hour = 24; t.minute = 0; t.enabled = true;
    TEST_ASSERT_FALSE(nht_sched_add(&S, &t, NULL));
    t.hour = 6; t.minute = 60;
    TEST_ASSERT_FALSE(nht_sched_add(&S, &t, NULL));
}

static void test_table_full(void)
{
    for (int i = 0; i < NHT_SCHED_MAX_TASKS; i++)
        add_task((uint8_t)(i % 24), 0, NHT_SCHED_REPEAT_DAILY, 60);

    nht_sched_task_t t;
    memset(&t, 0, sizeof(t));
    t.hour = 5; t.enabled = true;
    TEST_ASSERT_FALSE(nht_sched_add(&S, &t, NULL));
}

/* --- đổi thời gian --- */

static void test_time_from_unix(void)
{
    /* 2026-08-26 12:00:00 UTC = 20691*86400 + 43200 = 1787745600
       +07:00 -> 19:00 cùng ngày, thứ Tư */
    nht_time_t t;
    nht_time_from_unix(1787745600LL, 7 * 3600, &t);
    TEST_ASSERT_EQUAL_UINT32(DAY_WED, t.day);
    TEST_ASSERT_EQUAL_UINT8(19, t.hour);
    TEST_ASSERT_EQUAL_UINT8(0, t.minute);
    TEST_ASSERT_EQUAL_UINT8(3, t.wday);   /* thứ Tư */
}

static void test_next_in(void)
{
    add_task(6, 0, NHT_SCHED_REPEAT_DAILY, 900);
    nht_time_t now = at(DAY_WED, 3, 5, 30, 0);
    TEST_ASSERT_EQUAL_UINT32(30 * 60, nht_sched_next_in(&S, &now));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_due_at_exact_time);
    RUN_TEST(test_not_due_before_time);
    RUN_TEST(test_no_time_no_run);
    RUN_TEST(test_catchup_runs_when_tick_is_late);
    RUN_TEST(test_catchup_gives_up_after_window);
    RUN_TEST(test_runs_only_once_per_day);
    RUN_TEST(test_update_actually_persists);
    RUN_TEST(test_update_unknown_id_fails);
    RUN_TEST(test_update_time_allows_rerun_today);
    RUN_TEST(test_weekday_mask);
    RUN_TEST(test_one_shot_disables_itself);
    RUN_TEST(test_add_remove_ids_unique);
    RUN_TEST(test_reject_invalid_time);
    RUN_TEST(test_table_full);
    RUN_TEST(test_time_from_unix);
    RUN_TEST(test_next_in);
    return UNITY_END();
}
