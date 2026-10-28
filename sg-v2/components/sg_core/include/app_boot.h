#ifndef APP_BOOT_H
#define APP_BOOT_H

#include "esp_system.h"

/** Gọi đầu app_main, trước mọi thứ khác: ghi lại vì sao lần khởi động này xảy ra. */
void app_boot_capture(void);

esp_reset_reason_t app_last_reset_reason(void);
const char *app_last_reset_str(void);

/** true nếu lần reset trước là sự cố (panic, watchdog, sụt áp), không phải chủ ý. */
bool app_last_reset_abnormal(void);

#endif
