/**
 * Hồi quy cho nht_actuator.
 * test_repeated_on_* là bản thu nhỏ của lỗi "tưới 60 giây thay vì 15 phút"
 * ở bản Arduino trước: một lệnh bật lặp lại làm mất thời lượng đã đặt.
 */
#include <unity.h>
#include <string.h>
#include "nht_actuator.h"

static nht_act_t A;
static bool  pin_level;       /* mức logic THẬT ghi ra chân */
static int   writes;
static nht_act_event_t last_ev;
static int   ev_count;

static void fake_write(int pin, bool level, void *user)
{
    (void)pin; (void)user;
    pin_level = level;
    writes++;
}

static void on_ev(nht_act_t *a, nht_act_event_t ev, void *user)
{
    (void)a; (void)user;
    last_ev = ev;
    ev_count++;
}

static void mk(uint32_t auto_off_ms, uint32_t on_delay_ms, bool active_high)
{
    nht_act_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.pin         = 5;
    cfg.active_high = active_high;
    cfg.auto_off_ms = auto_off_ms;
    cfg.on_delay_ms = on_delay_ms;
    cfg.write       = fake_write;
    cfg.name        = "test";

    writes = 0; ev_count = 0; last_ev = NHT_ACT_EV_OFF;
    nht_act_init(&A, &cfg, 0);
    nht_act_on_event(&A, on_ev, NULL);
}

void setUp(void)    { mk(60000, 0, true); }
void tearDown(void) { }

/* --- cơ bản --- */

static void test_on_off(void)
{
    TEST_ASSERT_EQUAL(NHT_ACT_OK, nht_act_on(&A, 0));
    TEST_ASSERT_TRUE(nht_act_is_on(&A));
    TEST_ASSERT_TRUE(pin_level);
    TEST_ASSERT_EQUAL(NHT_ACT_EV_ON, last_ev);

    TEST_ASSERT_EQUAL(NHT_ACT_OK, nht_act_off(&A, 1000));
    TEST_ASSERT_FALSE(nht_act_is_on(&A));
    TEST_ASSERT_FALSE(pin_level);
    TEST_ASSERT_EQUAL(NHT_ACT_EV_OFF, last_ev);
}

static void test_active_low_inverts_pin(void)
{
    mk(0, 0, false);                  /* relay kích mức thấp */
    TEST_ASSERT_TRUE(pin_level);      /* tắt  -> chân ở mức CAO */
    nht_act_on(&A, 0);
    TEST_ASSERT_FALSE(pin_level);     /* bật  -> chân ở mức THẤP */
    nht_act_off(&A, 1000);
    TEST_ASSERT_TRUE(pin_level);
}

static void test_auto_off(void)
{
    nht_act_on(&A, 0);
    nht_act_tick(&A, 59999);
    TEST_ASSERT_TRUE(nht_act_is_on(&A));

    nht_act_tick(&A, 60000);
    TEST_ASSERT_FALSE(nht_act_is_on(&A));
    TEST_ASSERT_EQUAL(NHT_ACT_EV_AUTO_OFF, last_ev);
}

static void test_zero_auto_off_stays_on(void)
{
    mk(0, 0, true);
    nht_act_on(&A, 0);
    nht_act_tick(&A, 86400000u);      /* cả ngày */
    TEST_ASSERT_TRUE(nht_act_is_on(&A));
    TEST_ASSERT_EQUAL_UINT32(0, nht_act_remaining_ms(&A, 1000));
}

/* --- LỖI v1: lệnh bật lặp lại làm mất thời lượng --- */

static void test_repeated_on_for_is_idempotent(void)
{
    const uint32_t FIFTEEN_MIN = 900000u;

    TEST_ASSERT_EQUAL(NHT_ACT_OK, nht_act_on_for(&A, FIFTEEN_MIN, 0));
    TEST_ASSERT_EQUAL_UINT32(FIFTEEN_MIN, nht_act_remaining_ms(&A, 0));

    /* Lệnh y hệt dội về sau 2 giây (event đồng bộ từ cloud chẳng hạn).
       Hạn phải KHÔNG đổi - không dời, không rút ngắn. */
    nht_act_on_for(&A, FIFTEEN_MIN, 2000);
    TEST_ASSERT_EQUAL_UINT32(FIFTEEN_MIN - 2000, nht_act_remaining_ms(&A, 2000));

    /* Và van vẫn mở sau mốc 60 giây - chính là chỗ v1 đóng sớm. */
    nht_act_tick(&A, 61000);
    TEST_ASSERT_TRUE(nht_act_is_on(&A));

    nht_act_tick(&A, FIFTEEN_MIN);
    TEST_ASSERT_FALSE(nht_act_is_on(&A));
}

static void test_plain_on_does_not_clobber_custom_duration(void)
{
    const uint32_t FIFTEEN_MIN = 900000u;
    nht_act_on_for(&A, FIFTEEN_MIN, 0);

    /* on() trần trụi khi đang bật KHÔNG được kéo hạn về auto_off_ms (60s). */
    TEST_ASSERT_EQUAL(NHT_ACT_OK, nht_act_on(&A, 5000));
    TEST_ASSERT_EQUAL_UINT32(FIFTEEN_MIN - 5000, nht_act_remaining_ms(&A, 5000));

    nht_act_tick(&A, 61000);
    TEST_ASSERT_TRUE(nht_act_is_on(&A));
}

static void test_sync_is_guarded(void)
{
    const uint32_t FIFTEEN_MIN = 900000u;
    nht_act_on_for(&A, FIFTEEN_MIN, 0);
    int writes_before = writes;

    /* Trạng thái báo về trùng trạng thái hiện tại -> không được làm gì cả. */
    TEST_ASSERT_EQUAL(NHT_ACT_OK, nht_act_sync(&A, true, 3000));
    TEST_ASSERT_EQUAL_INT(writes_before, writes);
    TEST_ASSERT_EQUAL_UINT32(FIFTEEN_MIN - 3000, nht_act_remaining_ms(&A, 3000));

    /* Khác trạng thái thì mới thực sự tắt. */
    TEST_ASSERT_EQUAL(NHT_ACT_OK, nht_act_sync(&A, false, 4000));
    TEST_ASSERT_FALSE(nht_act_is_on(&A));
}

static void test_extend_adds_from_now(void)
{
    nht_act_on_for(&A, 10000, 0);
    nht_act_extend(&A, 5000, 3000);
    TEST_ASSERT_EQUAL_UINT32(12000, nht_act_remaining_ms(&A, 3000));
}

/* --- trễ bật --- */

static void test_on_delay(void)
{
    mk(60000, 8000, true);
    nht_act_on(&A, 0);
    TEST_ASSERT_FALSE(nht_act_is_on(&A));
    TEST_ASSERT_FALSE(pin_level);

    nht_act_tick(&A, 7999);
    TEST_ASSERT_FALSE(pin_level);

    nht_act_tick(&A, 8000);
    TEST_ASSERT_TRUE(nht_act_is_on(&A));
    TEST_ASSERT_TRUE(pin_level);

    /* Thời lượng đếm từ lúc ĐÓNG MẠCH, không phải từ lúc nhận lệnh. */
    TEST_ASSERT_EQUAL_UINT32(60000, nht_act_remaining_ms(&A, 8000));
}

/* --- khoá an toàn --- */

static void test_inhibit_forces_off_and_blocks(void)
{
    nht_act_on_for(&A, 900000u, 0);
    TEST_ASSERT_TRUE(nht_act_is_on(&A));

    nht_act_set_inhibit(&A, true, 1000);
    TEST_ASSERT_FALSE(nht_act_is_on(&A));
    TEST_ASSERT_EQUAL(NHT_ACT_EV_INHIBITED, last_ev);

    TEST_ASSERT_EQUAL(NHT_ACT_ERR_INHIBIT, nht_act_on(&A, 2000));
    TEST_ASSERT_EQUAL(NHT_ACT_ERR_INHIBIT, nht_act_on_for(&A, 5000, 2000));
    TEST_ASSERT_FALSE(nht_act_is_on(&A));

    nht_act_set_inhibit(&A, false, 3000);
    TEST_ASSERT_EQUAL(NHT_ACT_OK, nht_act_on(&A, 3000));
}

/* --- chống đảo nhanh --- */

static void test_min_off_blocks_rapid_restart(void)
{
    nht_act_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.pin = 5; cfg.active_high = true; cfg.write = fake_write;
    cfg.min_off_ms = 30000;
    nht_act_init(&A, &cfg, 0);
    nht_act_on_event(&A, on_ev, NULL);

    nht_act_on(&A, 0);
    nht_act_off(&A, 1000);
    TEST_ASSERT_EQUAL(NHT_ACT_ERR_MIN_OFF, nht_act_on(&A, 2000));
    TEST_ASSERT_EQUAL(NHT_ACT_OK, nht_act_on(&A, 31000));
}

static void test_min_on_blocks_early_off_but_force_wins(void)
{
    nht_act_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.pin = 5; cfg.active_high = true; cfg.write = fake_write;
    cfg.min_on_ms = 30000;
    nht_act_init(&A, &cfg, 0);
    nht_act_on_event(&A, on_ev, NULL);

    nht_act_on(&A, 0);
    TEST_ASSERT_EQUAL(NHT_ACT_ERR_MIN_ON, nht_act_off(&A, 1000));
    TEST_ASSERT_TRUE(nht_act_is_on(&A));

    /* Dừng khẩn cấp phải luôn thắng - an toàn không được xếp sau tuổi thọ bơm. */
    TEST_ASSERT_EQUAL(NHT_ACT_OK, nht_act_off_force(&A, 1000));
    TEST_ASSERT_FALSE(nht_act_is_on(&A));
}

/* --- tràn millis --- */

static void test_survives_tick_wraparound(void)
{
    uint32_t near_max = 0xFFFFF000u;
    mk(60000, 0, true);
    nht_act_on(&A, near_max);
    TEST_ASSERT_TRUE(nht_act_is_on(&A));

    /* Vượt qua mốc tràn 32 bit. */
    nht_act_tick(&A, near_max + 59000u);
    TEST_ASSERT_TRUE(nht_act_is_on(&A));

    nht_act_tick(&A, near_max + 60000u);
    TEST_ASSERT_FALSE(nht_act_is_on(&A));
}

static void test_startup_on(void)
{
    nht_act_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.pin = 5; cfg.active_high = true; cfg.write = fake_write;
    cfg.startup = NHT_ACT_START_ON;
    nht_act_init(&A, &cfg, 0);
    nht_act_on_event(&A, on_ev, NULL);
    TEST_ASSERT_TRUE(nht_act_is_on(&A));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_on_off);
    RUN_TEST(test_active_low_inverts_pin);
    RUN_TEST(test_auto_off);
    RUN_TEST(test_zero_auto_off_stays_on);
    RUN_TEST(test_repeated_on_for_is_idempotent);
    RUN_TEST(test_plain_on_does_not_clobber_custom_duration);
    RUN_TEST(test_sync_is_guarded);
    RUN_TEST(test_extend_adds_from_now);
    RUN_TEST(test_on_delay);
    RUN_TEST(test_inhibit_forces_off_and_blocks);
    RUN_TEST(test_min_off_blocks_rapid_restart);
    RUN_TEST(test_min_on_blocks_early_off_but_force_wins);
    RUN_TEST(test_survives_tick_wraparound);
    RUN_TEST(test_startup_on);
    return UNITY_END();
}
