/**
 * board_config.h - Pin map ESP32-C3 Super Mini
 *
 * ĐÂY LÀ BẢN PLACEHOLDER. Sửa lại theo PCB v2 thật rồi build lại.
 * Đổi pin chỉ cần sửa file này, không đụng vào code khác.
 *
 * Ràng buộc của ESP32-C3 Super Mini (KHÁC HẲN ESP32 của v1 - pin
 * 16/18/19/32/34/35 trong v1 không tồn tại trên C3):
 *
 *   GPIO khả dụng : 0..10, 20, 21   (tổng 13 chân)
 *   ADC1          : GPIO 0,1,2,3,4  <- cảm biến analog BẮT BUỘC nằm trong đây
 *   ADC2          : GPIO 5          <- không dùng được khi WiFi bật. Tránh.
 *   Strapping     : GPIO 2, 8, 9    <- không treo tải kéo mức lúc boot
 *   LED onboard   : GPIO 8 (active LOW)
 *   Nút BOOT      : GPIO 9
 *   UART0         : GPIO 20 (RX) / 21 (TX) - để dành cho serial console
 */

#ifndef SG_BOARD_CONFIG_H
#define SG_BOARD_CONFIG_H

/* --- Relay điều khiển van (van bi motor kiểu v1 dùng cả 4) --- */
#define PIN_VALVE_POWER      5   /* R1 - cấp điện cho motor van */
#define PIN_VALVE_DIRECTION  6   /* R2 - chiều quay: off=mở, on=đóng */
#define PIN_PUMP_POWER       7   /* R3 - bơm */
#define PIN_AC_POWER        10   /* R4 - nguồn AC tổng */

/* Van solenoid NC chỉ cần 1 relay, dùng lại chân R1 */
#define PIN_VALVE_SOLENOID   PIN_VALVE_POWER

/* Relay trên PCB v1/v2 kích mức thấp */
#define RELAY_ACTIVE_LEVEL   0

/* --- Cảm biến --- */
#define PIN_LEAK_ADC         3   /* ADC1_CH3 - điện cực trần (chế độ analog) */
#define PIN_LEAK_DIGITAL     1   /* module có comparator (chế độ digital)   */
#define PIN_VOLTAGE_ADC      4   /* ADC1_CH4 - cầu phân áp đo nguồn         */
#define PIN_FLOW_SENSOR      0   /* xung lưu lượng kế -> PCNT               */

/* --- Giao diện người dùng trên board --- */
#define PIN_STATUS_LED       8   /* LED onboard, active LOW */
#define LED_ACTIVE_LEVEL     0
#define PIN_BUTTON           9   /* nút BOOT: giữ 5s = xoá config, khôi phục mặc định */

/* Cầu phân áp đo nguồn: Vin = Vadc * (R1+R2)/R2 */
#define VOLTAGE_DIVIDER_RATIO   11.0f   /* R1=100k, R2=10k */

#endif /* SG_BOARD_CONFIG_H */
