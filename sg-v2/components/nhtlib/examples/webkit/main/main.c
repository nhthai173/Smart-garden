/**
 * @file main.c
 * @brief nht_webkit example: a JSON endpoint, a WebSocket broadcast and OTA.
 *
 * Browse to http://<device-ip>/api/hello, or upload firmware to /update.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "nht_net.h"
#include "nht_webkit.h"

#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"

/* GET /api/hello?name=Bob  ->  {"hello":"Bob"} */
static esp_err_t h_hello(httpd_req_t *r)
{
    char name[24] = "world";
    nht_web_query(r, "name", name, sizeof name);   /* keeps the default if absent */
    char json[64];
    snprintf(json, sizeof json, "{\"hello\":\"%s\"}", name);
    return nht_web_json(r, json);
}

void app_main(void)
{
    nvs_flash_init();
    nht_net_cfg_t net = { .sta_ssid = WIFI_SSID, .sta_pass = WIFI_PASS };
    nht_net_start(&net);

    nht_web_cfg_t web = { .enable_ws = true, .enable_ota = true };
    nht_web_start(&web);
    nht_web_route("/api/hello", HTTP_GET, h_hello, NULL);

    while (1) {                                    /* push uptime to WebSocket clients */
        char msg[48];
        snprintf(msg, sizeof msg, "{\"uptime_s\":%lld}", esp_timer_get_time() / 1000000);
        nht_web_ws_broadcast(msg);                 /* safe with no clients connected */
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
