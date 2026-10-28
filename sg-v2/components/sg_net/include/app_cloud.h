#ifndef APP_CLOUD_H
#define APP_CLOUD_H
#include "esp_err.h"
#include <stdbool.h>
/** Starts only if the Firebase settings are complete. Returns ESP_ERR_INVALID_STATE otherwise. */
esp_err_t app_cloud_start(void);
bool app_cloud_online(void);
#endif
