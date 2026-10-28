/**
 * Regression tests for nht_actuator.
 * test_repeated_on_* is a miniature of the "60 seconds instead of 15 minutes" bug
 * of the earlier Arduino build: a repeated on command lost the duration already set.
 */
#include <unity.h>
#include <string.h>
#include "nht_actuator.h"

static nht_act_t A;
static bool  pin_level;       /* the REAL logic level written to the pin */
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

/* --- basics --- */

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
    mk(0, 0, false);                  /* active-low relay */
    TEST_ASSERT_TRUE(pin_level);      /* off -> pin HIGH */
    nht_act_on(&A, 0);
    TEST_ASSERT_FALSE(pin_level);     /* on  -> pin LOW  */
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
    nht_act_tick(&A, 86400000u);      /* a whole day */
    TEST_ASSERT_TRUE(nht_act_is_on(&A));
    TEST_ASSERT_EQUAL_UINT32(0, nht_act_remaining_ms(&A, 1000));
}

/* --- v1 BUG: a repeated on command lost the duration --- */

static void test_repeated_on_for_is_idempotent(void)
{
    const uint32_t FIFTEEN_MIN = 900000u;

    TEST_ASSERT_EQUAL(NHT_ACT_OK, nht_act_on_for(&A, FIFTEEN_MIN, 0));
    TEST_ASSERT_EQUAL_UINT32(FIFTEEN_MIN, nht_act_remaining_ms(&A, 0));

    /* The identical command echoes back after 2 seconds (a sync event from the cloud, say).
       The deadline must NOT change - neither moved nor shortened. */
    nht_act_on_for(&A, FIFTEEN_MIN, 2000);
    TEST_ASSERT_EQUAL_UINT32(FIFTEEN_MIN - 2000, nht_act_remaining_ms(&A, 2000));

    /* And the valve is still open after the 60-second mark - exactly where v1 closed early. */
    nht_act_tick(&A, 61000);
    TEST_ASSERT_TRUE(nht_act_is_on(&A));

    nht_act_tick(&A, FIFTEEN_MIN);
    TEST_ASSERT_FALSE(nht_act_is_on(&A));
}

static void test_plain_on_does_not_clobber_custom_duration(void)
{
    const uint32_t FIFTEEN_MIN = 900000u;
    nht_act_on_for(&A, FIFTEEN_MIN, 0);

    /* A bare on() while already on must NOT pull the deadline back to auto_off_ms (60 s). */
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

    /* A reported state equal to the current one -> must do nothing at all. */
    TEST_ASSERT_EQUAL(NHT_ACT_OK, nht_act_sync(&A, true, 3000));
    TEST_ASSERT_EQUAL_INT(writes_before, writes);
    TEST_ASSERT_EQUAL_UINT32(FIFTEEN_MIN - 3000, nht_act_remaining_ms(&A, 3000));

    /* Only a different state really switches it off. */
    TEST_ASSERT_EQUAL(NHT_ACT_OK, nht_act_sync(&A, false, 4000));
    TEST_ASSERT_FALSE(nht_act_is_on(&A));
}

static void test_extend_adds_from_now(void)
{
    nht_act_on_for(&A, 10000, 0);
    nht_act_extend(&A, 5000, 3000);
    TEST_ASSERT_EQUAL_UINT32(12000, nht_act_remaining_ms(&A, 3000));
}

/* --- turn-on delay --- */

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

    /* The duration counts from when the circuit CLOSES, not from when the command arrived. */
    TEST_ASSERT_EQUAL_UINT32(60000, nht_act_remaining_ms(&A, 8000));
}

/* --- safety inhibit --- */

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

/* --- anti-chatter --- */

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

    /* An emergency stop must always win - safety is not queued behind pump lifetime. */
    TEST_ASSERT_EQUAL(NHT_ACT_OK, nht_act_off_force(&A, 1000));
    TEST_ASSERT_FALSE(nht_act_is_on(&A));
}

/* --- millis() wrap-around --- */

static void test_survives_tick_wraparound(void)
{
    uint32_t near_max = 0xFFFFF000u;
    mk(60000, 0, true);
    nht_act_on(&A, near_max);
    TEST_ASSERT_TRUE(nht_act_is_on(&A));

    /* Cross the 32-bit wrap point. */
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
