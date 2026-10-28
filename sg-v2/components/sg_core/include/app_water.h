/**
 * app_water.h - Logic tưới cây. Đây là "nguồn sự thật" của thiết bị.
 *
 * Tầng này KHÔNG biết gì về WiFi, HTTP hay Firebase. Web server và Firebase
 * adapter đều gọi vào đây; chúng là phần thêm vào, không phải phần lõi.
 * Rút mạng ra thì hai adapter im lặng, còn lịch tưới vẫn chạy nguyên vẹn.
 *
 * Đây chính là chỗ hệ cũ làm ngược: danh sách lịch chỉ được nạp trong
 * callback "đã kết nối Firebase lần đầu", nên mất mạng lúc khởi động là
 * không tưới gì cả.
 */
#ifndef APP_WATER_H
#define APP_WATER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "nht_valve.h"
#include "nht_sched.h"

/** Ai ra lệnh - dùng cho log và để adapter biết có phải lệnh của mình không. */
typedef enum {
    WSRC_BOOT = 0,
    WSRC_LOCAL_API,
    WSRC_SCHEDULE,
    WSRC_CLOUD,
    WSRC_BUTTON,
    WSRC_SAFETY,
} water_src_t;

/**
 * Sự cố đang giữ van đóng. Bất kỳ bit nào bật = van bị ĐÓNG NGAY và khoá mở
 * lại; chỉ khi mọi bit tắt (đã qua lọc chống nhiễu) van mới được mở tiếp.
 */
#define WF_LEAK         0x01   /**< phát hiện ngập nước                       */
#define WF_LEAK_SENSOR  0x02   /**< cảm biến ngập hỏng: không còn "mắt" nữa   */
#define WF_VOLTAGE      0x04   /**< nguồn sụt dưới ngưỡng: motor không đủ lực */

/** Payload của một lịch tưới. Phần "tưới cây" duy nhất nhét vào nht_sched. */
typedef struct {
    uint16_t duration_s;
    uint8_t  level_pct;   /**< 1..100, chỉ có tác dụng với van bi */
} water_task_t;

typedef struct {
    nht_valve_state_t valve;
    uint32_t remaining_s;
    bool     leak;
    uint16_t leak_raw;
    float    voltage;
    bool     voltage_low;
    float    flow_lpm;
    float    flow_total_l;
    uint32_t uptime_s;
    water_src_t last_src;
    uint8_t  sched_count;
    bool     time_valid;
    uint8_t  faults;      /**< mặt nạ WF_*; 0 = bình thường */
} water_status_t;

/** Gọi khi có thay đổi đáng báo ra ngoài (đổi trạng thái van, ngập nước...). */
typedef void (*water_change_cb_t)(const water_status_t *st, void *user);

esp_err_t water_init(void);
esp_err_t water_start(void);

/**
 * Đẩy các thông số vừa đổi trong app_cfg vào cấu trúc ĐANG CHẠY.
 *
 * Chỉnh ngưỡng cảm biến ngập ngoài vườn mà phải khởi động lại mới có hiệu
 * lực thì không ai chỉnh nổi - phải thử vài vòng mới ra được con số đúng.
 *
 * Những thứ KHÔNG đổi nóng được (phải khởi động lại): loại van, chuyển giữa
 * analog/digital, và các chân GPIO - vì chúng quyết định ngoại vi nào được
 * cấp phát lúc khởi tạo.
 */
void water_apply_cfg(void);

/** Đăng ký người nghe. Tối đa WATER_MAX_LISTENERS. */
bool water_on_change(water_change_cb_t cb, void *user);

/** Phát tín hiệu thay đổi ngay (dùng khi adapter vừa kết nối xong). */
void water_notify(void);

/**
 * @param seconds 0 = dùng thời lượng mặc định. Luôn bị kẹp xuống run_max_s.
 * @param level   0 = 100%.
 */
nht_valve_err_t water_open(uint32_t seconds, uint8_t level, water_src_t src);
nht_valve_err_t water_close(water_src_t src);

void water_get_status(water_status_t *out);
int  water_status_json(char *buf, size_t len);

/* --- lịch tưới --- */
int  water_sched_json(char *buf, size_t len);
bool water_sched_add(uint8_t hour, uint8_t minute, uint8_t repeat_mask,
                     uint16_t duration_s, uint8_t level, bool enabled,
                     uint8_t *out_id);
bool water_sched_update(uint8_t id, uint8_t hour, uint8_t minute,
                        uint8_t repeat_mask, uint16_t duration_s,
                        uint8_t level, bool enabled);
bool water_sched_remove(uint8_t id);
void water_sched_clear(void);

/** Thay toàn bộ danh sách lịch bằng một mảng JSON (dùng khi đồng bộ cloud). */
bool water_sched_replace_json(const char *json);

const char *water_src_str(water_src_t s);

/** "leak+voltage" ... ; "" khi không có sự cố. buf >= 40 byte. */
const char *water_faults_str(uint8_t faults, char *buf, size_t len);

#if defined(SG_BENCH)
/* --- Chỉ có ở bản bench: mô phỏng I/O và gây sự cố để kiểm thử trên board
   thật khi chưa đấu cảm biến/relay. KHÔNG có trong firmware phát hành. --- */
void water_bench_set_leak_raw(uint16_t raw);
void water_bench_set_volt_raw(uint16_t raw);
void water_bench_fail_leak_adc(bool fail);
/** Treo task điều khiển `ms` mili giây để chứng minh watchdog đóng van. */
void water_bench_stall_ctl(uint32_t ms);
/** Mức THẬT đang xuất ra từng chân relay (đọc thanh ghi output). */
int  water_bench_pins_json(char *buf, size_t len);
#endif

#endif /* APP_WATER_H */
