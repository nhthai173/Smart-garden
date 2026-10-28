/**
 * Hồi quy cho nht_dinput.
 * test_flapping_* tái hiện đúng bệnh báo ảo của bản Arduino trước: cảm biến
 * ngập nước bị ẩm, tín hiệu dao động quanh ngưỡng, báo ngập/hết ngập qua lại
 * suốt hàng giờ.
 */
#include <unity.h>
#include <string.h>
#include "nht_dinput.h"

#define THR_ON   2400
#define THR_OFF  1200
#define HOLD_ON     5000u    /* 5 giây  */
#define HOLD_OFF  600000u    /* 10 phút */

static nht_dinput_t IN;
static int cb_calls;

static void on_change(nht_dinput_t *in, nht_din_state_t st, void *user)
{
    (void)in; (void)st; (void)user;
    cb_calls++;
}

static void mk(bool analog)
{
    memset(&IN, 0, sizeof(IN));
    IN.analog      = analog;
    IN.thr_on      = THR_ON;
    IN.thr_off     = THR_OFF;
    IN.hold_on_ms  = HOLD_ON;
    IN.hold_off_ms = HOLD_OFF;
    IN.on_change   = on_change;
    nht_dinput_init(&IN, NHT_DIN_INACTIVE);
    cb_calls = 0;
}

void setUp(void)    { mk(true); }
void tearDown(void) { }

/* Nạp `raw` liên tục trong `ms` mili giây, mỗi 200ms một mẫu. */
static uint32_t hold(uint16_t raw, uint32_t ms, uint32_t t0)
{
    for (uint32_t t = 0; t <= ms; t += 200)
        nht_dinput_feed(&IN, raw, t0 + t);
    return t0 + ms;
}

/* --- cơ bản --- */

static void test_needs_hold_to_assert(void)
{
    nht_dinput_feed(&IN, 3000, 0);
    TEST_ASSERT_FALSE(nht_dinput_active(&IN));       /* chưa đủ giờ */

    hold(3000, HOLD_ON - 400, 0);
    TEST_ASSERT_FALSE(nht_dinput_active(&IN));

    hold(3000, HOLD_ON + 400, 0);
    TEST_ASSERT_TRUE(nht_dinput_active(&IN));
    TEST_ASSERT_EQUAL_UINT32(1, IN.transitions);
}

static void test_asymmetric_hold(void)
{
    uint32_t t = hold(3000, HOLD_ON + 400, 0);
    TEST_ASSERT_TRUE(nht_dinput_active(&IN));

    /* Khô hẳn, nhưng mới 1 phút - chưa đủ 10 phút nên VẪN báo ngập. */
    t = hold(500, 60000, t);
    TEST_ASSERT_TRUE(nht_dinput_active(&IN));

    t = hold(500, HOLD_OFF + 1000, t);
    TEST_ASSERT_FALSE(nht_dinput_active(&IN));
    TEST_ASSERT_EQUAL_UINT32(2, IN.transitions);
}

/* --- LỖI v1: dao động quanh ngưỡng sinh chuỗi báo giả --- */

static void test_flapping_around_threshold_gives_one_transition(void)
{
    uint32_t t = 0;

    /* Cảm biến ẩm: giá trị nhảy quanh vùng giữa hai ngưỡng suốt 3 tiếng.
       Không mẫu nào chạm THR_OFF, nên sau khi đã công nhận "ngập" thì
       KHÔNG được có thêm chuyển trạng thái nào nữa. */
    t = hold(3000, HOLD_ON + 400, t);         /* ngập thật */
    TEST_ASSERT_TRUE(nht_dinput_active(&IN));

    const uint16_t noise[] = { 2350, 1900, 2500, 1450, 2600, 1300, 2200, 1800 };
    for (int cycle = 0; cycle < 900; cycle++) {  /* ~3 tiếng */
        for (unsigned i = 0; i < sizeof(noise) / sizeof(noise[0]); i++) {
            t += 1500;
            nht_dinput_feed(&IN, noise[i], t);
        }
    }

    TEST_ASSERT_TRUE(nht_dinput_active(&IN));
    TEST_ASSERT_EQUAL_UINT32(1, IN.transitions);   /* v1 sinh ra hàng chục */
    TEST_ASSERT_EQUAL_INT(1, cb_calls);
}

static void test_brief_dry_dip_does_not_clear(void)
{
    uint32_t t = hold(3000, HOLD_ON + 400, 0);

    /* Tụt hẳn xuống dưới THR_OFF nhưng chỉ 30 giây rồi ướt lại. */
    t = hold(300, 30000, t);
    t = hold(3000, 30000, t);

    TEST_ASSERT_TRUE(nht_dinput_active(&IN));
    TEST_ASSERT_EQUAL_UINT32(1, IN.transitions);
}

static void test_schmitt_deadband_blocks_assert(void)
{
    /* Đang khô, tín hiệu nằm giữa hai ngưỡng mãi -> không bao giờ thành ngập. */
    hold(1800, HOLD_ON * 10, 0);
    TEST_ASSERT_FALSE(nht_dinput_active(&IN));
    TEST_ASSERT_EQUAL_UINT32(0, IN.transitions);
}

/* --- chế độ digital --- */

static void test_digital_mode(void)
{
    mk(false);
    uint32_t t = hold(1, HOLD_ON + 400, 0);
    TEST_ASSERT_TRUE(nht_dinput_active(&IN));

    t = hold(0, 60000, t);
    TEST_ASSERT_TRUE(nht_dinput_active(&IN));      /* trễ ra vẫn áp dụng */

    hold(0, HOLD_OFF + 1000, t);
    TEST_ASSERT_FALSE(nht_dinput_active(&IN));
}

static void test_digital_inverted(void)
{
    mk(false);
    IN.inverted = true;
    nht_dinput_init(&IN, NHT_DIN_INACTIVE);

    hold(0, HOLD_ON + 400, 0);
    TEST_ASSERT_TRUE(nht_dinput_active(&IN));
}

static void test_analog_inverted(void)
{
    mk(true);
    IN.inverted = true;
    nht_dinput_init(&IN, NHT_DIN_INACTIVE);

    /* Đảo: giá trị THẤP nghĩa là có nước. 4095-500 = 3595 >= THR_ON. */
    hold(500, HOLD_ON + 400, 0);
    TEST_ASSERT_TRUE(nht_dinput_active(&IN));
}

/* --- cấu hình hỏng --- */

static void test_swapped_thresholds_are_repaired(void)
{
    memset(&IN, 0, sizeof(IN));
    IN.analog  = true;
    IN.thr_on  = 2000;
    IN.thr_off = 2500;          /* đảo ngược - Schmitt sẽ mất tác dụng */
    nht_dinput_init(&IN, NHT_DIN_INACTIVE);
    TEST_ASSERT_TRUE(IN.thr_off < IN.thr_on);
}

/* --- median --- */

static void test_median(void)
{
    uint16_t b[5] = { 900, 100, 4000, 120, 110 };
    TEST_ASSERT_EQUAL_UINT16(120, nht_median_u16(b, 5));

    uint16_t c[3] = { 7, 7, 7 };
    TEST_ASSERT_EQUAL_UINT16(7, nht_median_u16(c, 3));
    TEST_ASSERT_EQUAL_UINT16(0, nht_median_u16(NULL, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_needs_hold_to_assert);
    RUN_TEST(test_asymmetric_hold);
    RUN_TEST(test_flapping_around_threshold_gives_one_transition);
    RUN_TEST(test_brief_dry_dip_does_not_clear);
    RUN_TEST(test_schmitt_deadband_blocks_assert);
    RUN_TEST(test_digital_mode);
    RUN_TEST(test_digital_inverted);
    RUN_TEST(test_analog_inverted);
    RUN_TEST(test_swapped_thresholds_are_repaired);
    RUN_TEST(test_median);
    return UNITY_END();
}
