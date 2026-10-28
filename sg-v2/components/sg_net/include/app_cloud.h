#ifndef APP_CLOUD_H
#define APP_CLOUD_H
#include "esp_err.h"
#include <stdbool.h>
/** Chỉ khởi động nếu cấu hình Firebase đã đủ. Trả ESP_ERR_INVALID_STATE nếu chưa. */
esp_err_t app_cloud_start(void);
bool app_cloud_online(void);
#endif
