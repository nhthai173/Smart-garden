/**
 * Regression tests for nht_valve: the open/close sequences of ball and solenoid valves,
 * the hard run-time ceiling, the safety inhibit, and invariance under duplicate commands.
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
static bool pin[24];                 /* the REAL logic level on each pin */
static nht_valve_event_t last_ev;
static int ev_count;
static int dir_under_power;      /* times the direction relay switched while the motor was powered */

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

/** Run ticks from t0 to t1 in 100 ms steps. Returns t1. */
static uint32_t run_to(uint32_t t0, uint32_t t1)
{
    for (uint32_t t = t0; t <= t1; t += 100)
        nht_valve_tick(&V, t);
    return t1;
}

/* After init a ball valve still turns a full travel to close. T0 is when it is done. */
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

    /* Right away: direction = open, AC on, motor turning, valve OPENING. */
    TEST_ASSERT_EQUAL(NHT_VALVE_OPENING, nht_valve_state(&V));
    TEST_ASSERT_FALSE(pin[P_DIR]);
    TEST_ASSERT_TRUE(pin[P_AC]);
    TEST_ASSERT_TRUE(pin[P_POWER]);
    TEST_ASSERT_FALSE(pin[P_PUMP]);          /* the pump is still waiting out its delay */

    run_to(T0, T0 + PUMP_DELAY_MS + 200);
    TEST_ASSERT_TRUE(pin[P_PUMP]);

    /* End of travel: the motor cuts off, the valve becomes OPEN. */
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
    TEST_ASSERT_FALSE(pin[P_PUMP]);          /* pump off BEFORE the valve closes */
    TEST_ASSERT_TRUE(pin[P_DIR]);            /* direction = close */
    TEST_ASSERT_TRUE(pin[P_POWER]);

    t = run_to(t, t + TRAVEL_MS + 200);
    TEST_ASSERT_EQUAL(NHT_VALVE_CLOSED, nht_valve_state(&V));
    TEST_ASSERT_FALSE(pin[P_POWER]);
    TEST_ASSERT_FALSE(pin[P_DIR]);           /* the direction relay is released */
}

static void test_ball_partial_open_level(void)
{
    /* Opening 50% = powering the motor for half the travel. */
    nht_valve_open(&V, 900000u, 50, T0);
    run_to(T0, T0 + TRAVEL_MS / 2 - 300);
    TEST_ASSERT_TRUE(pin[P_POWER]);

    run_to(T0 + TRAVEL_MS / 2 - 300, T0 + TRAVEL_MS / 2 + 300);
    TEST_ASSERT_FALSE(pin[P_POWER]);
    TEST_ASSERT_EQUAL(NHT_VALVE_OPEN, nht_valve_state(&V));
}

static void test_init_always_closes(void)
{
    /* A ball valve must TURN to close at start-up, not merely release a relay: after a
       reset in the middle of watering the valve position is unknown. */
    mk(NHT_VALVE_BALL);
    TEST_ASSERT_TRUE(pin[P_DIR]);
    TEST_ASSERT_TRUE(pin[P_POWER]);
    TEST_ASSERT_EQUAL(NHT_VALVE_CLOSING, nht_valve_state(&V));

    run_to(0, TRAVEL_MS + 200);
    TEST_ASSERT_EQUAL(NHT_VALVE_CLOSED, nht_valve_state(&V));
    TEST_ASSERT_FALSE(pin[P_POWER]);
}

/* --- v1 BUG: the duration was corrupted by duplicate commands --- */

static void test_duplicate_open_keeps_duration(void)
{
    const uint32_t FIFTEEN = 900000u;
    nht_valve_open(&V, FIFTEEN, 100, T0);
    uint32_t t = run_to(T0, T0 + TRAVEL_MS + 200);

    /* The identical command echoes back (state sync) - must not shorten the run. */
    nht_valve_open(&V, FIFTEEN, 100, t);
    TEST_ASSERT_EQUAL(NHT_VALVE_OPEN, nht_valve_state(&V));

    /* Past default_run_ms = 60 s: the v1 valve closed here, this one does not. */
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
    TEST_ASSERT_FALSE(pin[P_POWER]);      /* the motor must NOT turn again */
}

/* --- safety --- */

static void test_max_run_is_a_hard_ceiling(void)
{
    /* A command asks for 10 hours - must be clamped to 30 minutes. */
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

    /* Reversing mid-travel -> the valve would stop at an unknown position. */
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

/* --- solenoid valve --- */

static void test_solenoid_opens_instantly(void)
{
    mk_ready(NHT_VALVE_SOLENOID);
    TEST_ASSERT_EQUAL(NHT_VALVE_CLOSED, nht_valve_state(&V));
    TEST_ASSERT_FALSE(pin[P_POWER]);

    nht_valve_open(&V, 900000u, 100, 0);
    TEST_ASSERT_EQUAL(NHT_VALVE_OPEN, nht_valve_state(&V));
    TEST_ASSERT_TRUE(pin[P_POWER]);          /* the coil is energised */
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
    TEST_ASSERT_FALSE(pin[P_POWER]);         /* power off = the spring closes the valve */
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

    /* AC counts from when the valve stands still (30 s), not from when it opened. */
    t = run_to(t, 30000 + AC_OFF_MS - 2000);
    TEST_ASSERT_TRUE(pin[P_AC]);
    run_to(t, 30000 + AC_OFF_MS + 2000);
    TEST_ASSERT_FALSE(pin[P_AC]);            /* the AC supply cuts itself */
}


/* --- reversal mid-travel / AC supply --- */

/* Real bug: closing while the valve was part-open only ran out the old deadline of the
   open, so the valve reported CLOSED while still part-open. */
static void test_close_during_opening_drives_full_travel(void)
{
    nht_valve_open(&V, 900000u, 100, T0);
    uint32_t t = run_to(T0, T0 + 7000);          /* 7 s into the 8 s opening travel */
    nht_valve_close(&V, t);
    TEST_ASSERT_EQUAL(NHT_VALVE_CLOSING, nht_valve_state(&V));

    /* Still in the direction pause: the motor must NOT be powered now. */
    TEST_ASSERT_FALSE(pin[P_POWER]);

    t = run_to(t, t + 200);                      /* the pause is over */
    TEST_ASSERT_TRUE(pin[P_DIR]);
    TEST_ASSERT_TRUE(pin[P_POWER]);

    /* It must run a FULL closing travel, not the 1 s left over from the opening. */
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

/* Real bug: AC was timed from the FIRST switch-on, so a close right after an open could
   lose power mid-travel. */
static void test_ac_survives_a_late_close(void)
{
    nht_valve_open(&V, 900000u, 100, T0);
    uint32_t t = run_to(T0, T0 + AC_OFF_MS - 3000);   /* close to the old AC deadline */
    nht_valve_close(&V, t);
    t = run_to(t, t + 6000);                          /* past the old AC deadline */
    TEST_ASSERT_TRUE(pin[P_AC]);
    TEST_ASSERT_EQUAL(NHT_VALVE_CLOSING, nht_valve_state(&V));
    run_to(t, t + TRAVEL_MS);
    TEST_ASSERT_EQUAL(NHT_VALVE_CLOSED, nht_valve_state(&V));
}

static void test_solenoid_keeps_ac_while_open(void)
{
    mk_ready(NHT_VALVE_SOLENOID);
    nht_valve_open(&V, 900000u, 100, 0);
    run_to(0, 600000u);                  /* 3 times AC_OFF_MS */
    TEST_ASSERT_EQUAL(NHT_VALVE_OPEN, nht_valve_state(&V));
    TEST_ASSERT_TRUE(pin[P_AC]);
}

/* --- event-driven waiting --- */

static void test_next_wake_tracks_deadlines(void)
{
    uint32_t ms;
    /* Idle, AC already off: no deadline at all. */
    run_to(T0, T0 + AC_OFF_MS + 1000);
    TEST_ASSERT_FALSE(nht_valve_next_wake_ms(&V, T0 + AC_OFF_MS + 1000, &ms));

    uint32_t t = T0 + AC_OFF_MS + 1000;
    nht_valve_open(&V, 900000u, 100, t);
    TEST_ASSERT_TRUE(nht_valve_next_wake_ms(&V, t, &ms));
    TEST_ASSERT_TRUE(ms <= PUMP_DELAY_MS);        /* the pump delay comes first, 3 s */

    /* Wait exactly that long, then tick: the right thing must happen. */
    uint32_t now = t;
    for (int guard = 0; guard < 20; guard++) {
        if (!nht_valve_next_wake_ms(&V, now, &ms)) break;
        now += ms ? ms : 1;
        nht_valve_tick(&V, now);
        if (nht_valve_state(&V) == NHT_VALVE_OPEN && pin[P_PUMP]) break;
    }
    TEST_ASSERT_EQUAL(NHT_VALVE_OPEN, nht_valve_state(&V));
    TEST_ASSERT_TRUE(pin[P_PUMP]);
    TEST_ASSERT_TRUE(now - t <= TRAVEL_MS + 50);  /* no blind ticking */

    /* Open: the next deadline is the end of the run time, far away. */
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
