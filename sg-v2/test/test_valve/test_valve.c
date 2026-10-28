/**
 * Hồi quy cho nht_valve: trình tự đóng/mở của van bi và van điện từ,
 * trần thời gian cứng, khoá an toàn, và tính bất biến trước lệnh trùng.
 */
#include <unity.h>
#include <string.h>
#include "nht_valve.h"

#define P_POWER 5
#define P_DIR   6
#define P_PUMP  7
#define P_AC    10

#define TRAVEL_MS      8000u
#define PUMP_DELAY_MS  3000u
#define AC_OFF_MS    180000u
#define DEFAULT_RUN   60000u
#define MAX_RUN     1800000u

static nht_valve_t V;
static bool pin[24];                 /* mức logic THẬT trên từng chân */
static nht_valve_event_t last_ev;
static int ev_count;
static int dir_under_power;      /* số lần relay chiều chuyển mạch khi động cơ có điện */

static void fake_write(int p, bool level, void *user)
{
    (void)user;
    if (p == P_DIR && pin[P_DIR] != level && pin[P_POWER]) dir_under_power++;
    if (p >= 0 && p < 24) pin[p] = level;
}

static void on_ev(nht_valve_t *v, nht_valve_event_t ev, void *user)
{
    (void)v; (void)user;
    last_ev = ev;
    ev_count++;
}

static void mk(nht_valve_type_t type)
{
    nht_valve_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.type           = type;
    cfg.pin_power      = P_POWER;
    cfg.pin_dir        = (type == NHT_VALVE_BALL) ? P_DIR : -1;
    cfg.pin_pump       = P_PUMP;
    cfg.pin_ac         = P_AC;
    cfg.active_high    = true;
    cfg.travel_ms      = TRAVEL_MS;
    cfg.pump_delay_ms  = PUMP_DELAY_MS;
    cfg.ac_auto_off_ms = AC_OFF_MS;
    cfg.default_run_ms = DEFAULT_RUN;
    cfg.max_run_ms     = MAX_RUN;
    cfg.write          = fake_write;

    memset(pin, 0, sizeof(pin));
    ev_count = 0;
    dir_under_power = 0;
    nht_valve_init(&V, &cfg, 0);
    nht_valve_on_event(&V, on_ev, NULL);
}

/** Chạy tick từ t0 tới t1, bước 100ms. Trả về t1. */
static uint32_t run_to(uint32_t t0, uint32_t t1)
{
    for (uint32_t t = t0; t <= t1; t += 100)
        nht_valve_tick(&V, t);
    return t1;
}

/* Sau init, van bi còn quay đủ một hành trình để đóng. T0 là lúc nó xong. */
#define T0 (TRAVEL_MS + 200u)

static void mk_ready(nht_valve_type_t type)
{
    mk(type);
    run_to(0, T0);
    ev_count = 0;
}

void setUp(void)    { mk_ready(NHT_VALVE_BALL); }
void tearDown(void) { }

/* --- van bi --- */

static void test_ball_open_sequence(void)
{
    TEST_ASSERT_EQUAL(NHT_VALVE_OK, nht_valve_open(&V, 900000u, 100, T0));

    /* Ngay lập tức: chiều = mở, AC bật, động cơ quay, van đang OPENING. */
    TEST_ASSERT_EQUAL(NHT_VALVE_OPENING, nht_valve_state(&V));
    TEST_ASSERT_FALSE(pin[P_DIR]);
    TEST_ASSERT_TRUE(pin[P_AC]);
    TEST_ASSERT_TRUE(pin[P_POWER]);
    TEST_ASSERT_FALSE(pin[P_PUMP]);          /* bơm còn đang trễ */

    run_to(T0, T0 + PUMP_DELAY_MS + 200);
    TEST_ASSERT_TRUE(pin[P_PUMP]);

    /* Hết hành trình: động cơ ngắt, van sang OPEN. */
    run_to(T0 + PUMP_DELAY_MS, T0 + TRAVEL_MS + 200);
    TEST_ASSERT_EQUAL(NHT_VALVE_OPEN, nht_valve_state(&V));
    TEST_ASSERT_FALSE(pin[P_POWER]);
    TEST_ASSERT_TRUE(pin[P_PUMP]);
}

static void test_ball_close_sequence(void)
{
    nht_valve_open(&V, 900000u, 100, T0);
    uint32_t t = run_to(T0, T0 + TRAVEL_MS + 200);

    nht_valve_close(&V, t);
    TEST_ASSERT_EQUAL(NHT_VALVE_CLOSING, nht_valve_state(&V));
    TEST_ASSERT_FALSE(pin[P_PUMP]);          /* bơm tắt TRƯỚC khi van khép */
    TEST_ASSERT_TRUE(pin[P_DIR]);            /* chiều = đóng */
    TEST_ASSERT_TRUE(pin[P_POWER]);

    t = run_to(t, t + TRAVEL_MS + 200);
    TEST_ASSERT_EQUAL(NHT_VALVE_CLOSED, nht_valve_state(&V));
    TEST_ASSERT_FALSE(pin[P_POWER]);
    TEST_ASSERT_FALSE(pin[P_DIR]);           /* nhả relay chiều */
}

static void test_ball_partial_open_level(void)
{
    /* Mở 50% = cấp điện động cơ nửa hành trình. */
    nht_valve_open(&V, 900000u, 50, T0);
    run_to(T0, T0 + TRAVEL_MS / 2 - 300);
    TEST_ASSERT_TRUE(pin[P_POWER]);

    run_to(T0 + TRAVEL_MS / 2 - 300, T0 + TRAVEL_MS / 2 + 300);
    TEST_ASSERT_FALSE(pin[P_POWER]);
    TEST_ASSERT_EQUAL(NHT_VALVE_OPEN, nht_valve_state(&V));
}

static void test_init_always_closes(void)
{
    /* Van bi phải QUAY để đóng lúc khởi động, không chỉ ngắt relay: sau khi
       tự reset giữa lúc tưới, vị trí van là không xác định. */
    mk(NHT_VALVE_BALL);
    TEST_ASSERT_TRUE(pin[P_DIR]);
    TEST_ASSERT_TRUE(pin[P_POWER]);
    TEST_ASSERT_EQUAL(NHT_VALVE_CLOSING, nht_valve_state(&V));

    run_to(0, TRAVEL_MS + 200);
    TEST_ASSERT_EQUAL(NHT_VALVE_CLOSED, nht_valve_state(&V));
    TEST_ASSERT_FALSE(pin[P_POWER]);
}

/* --- LỖI v1: thời lượng bị lệnh trùng làm hỏng --- */

static void test_duplicate_open_keeps_duration(void)
{
    const uint32_t FIFTEEN = 900000u;
    nht_valve_open(&V, FIFTEEN, 100, T0);
    uint32_t t = run_to(T0, T0 + TRAVEL_MS + 200);

    /* Lệnh y hệt dội về (đồng bộ trạng thái) - không được rút ngắn. */
    nht_valve_open(&V, FIFTEEN, 100, t);
    TEST_ASSERT_EQUAL(NHT_VALVE_OPEN, nht_valve_state(&V));

    /* Qua mốc default_run_ms = 60s: van v1 đóng ở đây, van này thì không. */
    t = run_to(t, T0 + 70000);
    TEST_ASSERT_TRUE(nht_valve_is_flowing(&V));

    t = run_to(t, T0 + FIFTEEN + 500);
    TEST_ASSERT_EQUAL(NHT_VALVE_CLOSING, nht_valve_state(&V));
    TEST_ASSERT_EQUAL(NHT_VALVE_EV_AUTO_CLOSED, last_ev);
}

static void test_duplicate_open_does_not_restart_motor(void)
{
    nht_valve_open(&V, 900000u, 100, T0);
    uint32_t t = run_to(T0, T0 + TRAVEL_MS + 200);
    TEST_ASSERT_FALSE(pin[P_POWER]);

    nht_valve_open(&V, 900000u, 100, t);
    TEST_ASSERT_FALSE(pin[P_POWER]);      /* động cơ KHÔNG được quay lại */
}

/* --- an toàn --- */

static void test_max_run_is_a_hard_ceiling(void)
{
    /* Lệnh xin 10 tiếng - phải bị kẹp xuống 30 phút. */
    nht_valve_open(&V, 36000000u, 100, T0);
    uint32_t t = run_to(T0, T0 + TRAVEL_MS + 200);
    TEST_ASSERT_TRUE(nht_valve_remaining_ms(&V, t) <= MAX_RUN);

    t = run_to(t, T0 + MAX_RUN + 500);
    TEST_ASSERT_FALSE(nht_valve_is_flowing(&V));
}

static void test_inhibit_closes_and_blocks(void)
{
    nht_valve_open(&V, 900000u, 100, T0);
    uint32_t t = run_to(T0, T0 + TRAVEL_MS + 200);

    nht_valve_set_inhibit(&V, true, t);
    TEST_ASSERT_EQUAL(NHT_VALVE_EV_INHIBITED, last_ev);
    TEST_ASSERT_EQUAL(NHT_VALVE_CLOSING, nht_valve_state(&V));
    TEST_ASSERT_FALSE(pin[P_PUMP]);

    t = run_to(t, t + TRAVEL_MS + 200);
    TEST_ASSERT_EQUAL(NHT_VALVE_ERR_INHIBIT, nht_valve_open(&V, 60000, 100, t));
    TEST_ASSERT_EQUAL(NHT_VALVE_CLOSED, nht_valve_state(&V));

    nht_valve_set_inhibit(&V, false, t);
    TEST_ASSERT_EQUAL(NHT_VALVE_OK, nht_valve_open(&V, 60000, 100, t));
}

static void test_open_while_closing_is_refused(void)
{
    nht_valve_open(&V, 900000u, 100, T0);
    uint32_t t = run_to(T0, T0 + TRAVEL_MS + 200);
    nht_valve_close(&V, t);

    /* Đảo chiều giữa hành trình -> van đứng ở vị trí không xác định. */
    TEST_ASSERT_EQUAL(NHT_VALVE_ERR_BUSY,
                      nht_valve_open(&V, 60000, 100, t + 1000));
}

static void test_default_duration_when_zero(void)
{
    nht_valve_open(&V, 0, 0, T0);
    uint32_t t = run_to(T0, T0 + TRAVEL_MS + 200);
    TEST_ASSERT_EQUAL_UINT32(DEFAULT_RUN - (t - T0),
                             nht_valve_remaining_ms(&V, t));
}

/* --- van điện từ --- */

static void test_solenoid_opens_instantly(void)
{
    mk_ready(NHT_VALVE_SOLENOID);
    TEST_ASSERT_EQUAL(NHT_VALVE_CLOSED, nht_valve_state(&V));
    TEST_ASSERT_FALSE(pin[P_POWER]);

    nht_valve_open(&V, 900000u, 100, 0);
    TEST_ASSERT_EQUAL(NHT_VALVE_OPEN, nht_valve_state(&V));
    TEST_ASSERT_TRUE(pin[P_POWER]);          /* cuộn van giữ điện */
    TEST_ASSERT_EQUAL(NHT_VALVE_EV_OPENED, last_ev);

    run_to(0, PUMP_DELAY_MS + 200);
    TEST_ASSERT_TRUE(pin[P_PUMP]);
}

static void test_solenoid_closes_instantly(void)
{
    mk_ready(NHT_VALVE_SOLENOID);
    nht_valve_open(&V, 900000u, 100, 0);
    uint32_t t = run_to(0, PUMP_DELAY_MS + 200);

    nht_valve_close(&V, t);
    TEST_ASSERT_EQUAL(NHT_VALVE_CLOSED, nht_valve_state(&V));
    TEST_ASSERT_FALSE(pin[P_POWER]);         /* ngắt điện = lò xo đóng van */
    TEST_ASSERT_FALSE(pin[P_PUMP]);
}

static void test_solenoid_auto_close(void)
{
    mk_ready(NHT_VALVE_SOLENOID);
    nht_valve_open(&V, 30000, 100, 0);
    run_to(0, 30000 + 500);
    TEST_ASSERT_EQUAL(NHT_VALVE_CLOSED, nht_valve_state(&V));
    TEST_ASSERT_EQUAL(NHT_VALVE_EV_AUTO_CLOSED, last_ev);
    TEST_ASSERT_FALSE(pin[P_POWER]);
}

static void test_ac_auto_off(void)
{
    mk_ready(NHT_VALVE_SOLENOID);
    nht_valve_open(&V, 30000, 100, 0);
    uint32_t t = run_to(0, 30000 + 500);
    TEST_ASSERT_TRUE(pin[P_AC]);

    /* AC tính từ lúc van đứng yên (30s), không phải từ lúc mở. */
    t = run_to(t, 30000 + AC_OFF_MS - 2000);
    TEST_ASSERT_TRUE(pin[P_AC]);
    run_to(t, 30000 + AC_OFF_MS + 2000);
    TEST_ASSERT_FALSE(pin[P_AC]);            /* nguồn AC tự cắt */
}


/* --- đảo chiều giữa hành trình / nguồn AC --- */

/* Lỗi thật: đóng lúc đang mở dở thì động cơ chỉ chạy nốt phần hạn cũ của
   lần mở, van báo ĐÓNG trong khi còn hé mở. */
static void test_close_during_opening_drives_full_travel(void)
{
    nht_valve_open(&V, 900000u, 100, T0);
    uint32_t t = run_to(T0, T0 + 7000);          /* 7s trên 8s hành trình mở */
    nht_valve_close(&V, t);
    TEST_ASSERT_EQUAL(NHT_VALVE_CLOSING, nht_valve_state(&V));

    /* Còn nghỉ chiều: động cơ KHÔNG được có điện lúc này. */
    TEST_ASSERT_FALSE(pin[P_POWER]);

    t = run_to(t, t + 200);                      /* hết thời gian nghỉ */
    TEST_ASSERT_TRUE(pin[P_DIR]);
    TEST_ASSERT_TRUE(pin[P_POWER]);

    /* Phải quay đủ MỘT hành trình đóng, không phải 1s còn sót của lần mở. */
    t = run_to(t, t + TRAVEL_MS - 500);
    TEST_ASSERT_TRUE(pin[P_POWER]);
    TEST_ASSERT_EQUAL(NHT_VALVE_CLOSING, nht_valve_state(&V));

    run_to(t, t + 1000);
    TEST_ASSERT_FALSE(pin[P_POWER]);
    TEST_ASSERT_EQUAL(NHT_VALVE_CLOSED, nht_valve_state(&V));
    TEST_ASSERT_EQUAL(0, dir_under_power);
}

static void test_reversal_never_switches_dir_under_power(void)
{
    nht_valve_open(&V, 900000u, 100, T0);
    uint32_t t = run_to(T0, T0 + 3000);
    nht_valve_close(&V, t);
    run_to(t, t + TRAVEL_MS + 1000);
    TEST_ASSERT_EQUAL(0, dir_under_power);
}

/* Lỗi thật: AC tính hạn từ lần bật ĐẦU, nên đóng ngay sau khi mở có thể bị
   cắt điện giữa hành trình. */
static void test_ac_survives_a_late_close(void)
{
    nht_valve_open(&V, 900000u, 100, T0);
    uint32_t t = run_to(T0, T0 + AC_OFF_MS - 3000);   /* sát hạn AC cũ */
    nht_valve_close(&V, t);
    t = run_to(t, t + 6000);                          /* qua hạn AC cũ */
    TEST_ASSERT_TRUE(pin[P_AC]);
    TEST_ASSERT_EQUAL(NHT_VALVE_CLOSING, nht_valve_state(&V));
    run_to(t, t + TRAVEL_MS);
    TEST_ASSERT_EQUAL(NHT_VALVE_CLOSED, nht_valve_state(&V));
}

static void test_solenoid_keeps_ac_while_open(void)
{
    mk_ready(NHT_VALVE_SOLENOID);
    nht_valve_open(&V, 900000u, 100, 0);
    run_to(0, 600000u);                  /* gấp 3 lần AC_OFF_MS */
    TEST_ASSERT_EQUAL(NHT_VALVE_OPEN, nht_valve_state(&V));
    TEST_ASSERT_TRUE(pin[P_AC]);
}

/* --- chờ theo sự kiện --- */

static void test_next_wake_tracks_deadlines(void)
{
    uint32_t ms;
    /* Đứng yên, AC đã tắt: không có hạn nào. */
    run_to(T0, T0 + AC_OFF_MS + 1000);
    TEST_ASSERT_FALSE(nht_valve_next_wake_ms(&V, T0 + AC_OFF_MS + 1000, &ms));

    uint32_t t = T0 + AC_OFF_MS + 1000;
    nht_valve_open(&V, 900000u, 100, t);
    TEST_ASSERT_TRUE(nht_valve_next_wake_ms(&V, t, &ms));
    TEST_ASSERT_TRUE(ms <= PUMP_DELAY_MS);        /* bơm trễ 3s tới trước */

    /* Chờ đúng hạn đó rồi tick thì đúng việc phải xảy ra. */
    uint32_t now = t;
    for (int guard = 0; guard < 20; guard++) {
        if (!nht_valve_next_wake_ms(&V, now, &ms)) break;
        now += ms ? ms : 1;
        nht_valve_tick(&V, now);
        if (nht_valve_state(&V) == NHT_VALVE_OPEN && pin[P_PUMP]) break;
    }
    TEST_ASSERT_EQUAL(NHT_VALVE_OPEN, nht_valve_state(&V));
    TEST_ASSERT_TRUE(pin[P_PUMP]);
    TEST_ASSERT_TRUE(now - t <= TRAVEL_MS + 50);  /* không phải tick mò */

    /* Đang mở: hạn kế tiếp là hết thời lượng, rất xa. */
    TEST_ASSERT_TRUE(nht_valve_next_wake_ms(&V, now, &ms));
    TEST_ASSERT_TRUE(ms > 10000u);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ball_open_sequence);
    RUN_TEST(test_ball_close_sequence);
    RUN_TEST(test_ball_partial_open_level);
    RUN_TEST(test_init_always_closes);
    RUN_TEST(test_duplicate_open_keeps_duration);
    RUN_TEST(test_duplicate_open_does_not_restart_motor);
    RUN_TEST(test_max_run_is_a_hard_ceiling);
    RUN_TEST(test_inhibit_closes_and_blocks);
    RUN_TEST(test_open_while_closing_is_refused);
    RUN_TEST(test_default_duration_when_zero);
    RUN_TEST(test_solenoid_opens_instantly);
    RUN_TEST(test_solenoid_closes_instantly);
    RUN_TEST(test_solenoid_auto_close);
    RUN_TEST(test_ac_auto_off);
    RUN_TEST(test_close_during_opening_drives_full_travel);
    RUN_TEST(test_reversal_never_switches_dir_under_power);
    RUN_TEST(test_ac_survives_a_late_close);
    RUN_TEST(test_solenoid_keeps_ac_while_open);
    RUN_TEST(test_next_wake_tracks_deadlines);
    return UNITY_END();
}
