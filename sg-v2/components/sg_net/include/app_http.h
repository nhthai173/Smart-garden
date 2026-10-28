#ifndef APP_HTTP_H
#define APP_HTTP_H
#include "esp_err.h"
esp_err_t app_http_start(void);
int app_info_json(char *buf, size_t len);
#endif
