/**
 * @file main.c
 * @brief nht_fbrtdb example: sign in, listen to /desired, report to /reported.
 *
 * Fill in the WiFi and Firebase values below. The /desired and /reported split
 * keeps the device from reacting to its own writes.
 */
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "nht_net.h"
#include "nht_fbrtdb.h"

#define WIFI_SSID  "your-ssid"
#define WIFI_PASS  "your-password"
#define FB_API_KEY "your-web-api-key"
#define FB_DB_URL  "https://your-project-default-rtdb.firebasedatabase.app"
#define FB_EMAIL   "device@example.com"
#define FB_PASS    "device-password"

static const char *TAG = "example";
static bool s_snapshot;     /* true: the next "put /" is a snapshot, not a command */

/* Every stream event arrives here, from the stream task. */
static void on_event(const char *event, const char *path, const char *data, void *user)
{
    if (!strcmp(event, "connected")) { s_snapshot = true; return; }
    if (strcmp(event, "put") && strcmp(event, "patch")) return;

    if (s_snapshot && !strcmp(path, "/")) {
        s_snapshot = false;
        ESP_LOGI(TAG, "snapshot of /desired: %s", data);   /* synchronise only */
        return;
    }
    ESP_LOGI(TAG, "change at %s: %s", path, data);          /* a real command */
}

void app_main(void)
{
    nvs_flash_init();
    nht_net_cfg_t net = { .sta_ssid = WIFI_SSID, .sta_pass = WIFI_PASS };
    nht_net_start(&net);
    while (nht_net_state() != NHT_NET_CONNECTED) vTaskDelay(pdMS_TO_TICKS(500));

    nht_fb_cfg_t fb = { .api_key = FB_API_KEY, .db_url = FB_DB_URL,
                        .email = FB_EMAIL, .password = FB_PASS };
    nht_fb_start(&fb);                              /* signs in by itself */
    while (nht_fb_state() != NHT_FB_READY) vTaskDelay(pdMS_TO_TICKS(500));

    char path[160];
    snprintf(path, sizeof path, "/%s/example/desired", nht_fb_uid());
    nht_fb_stream_start(path, on_event, NULL);

    while (1) {                                     /* report once a minute */
        snprintf(path, sizeof path, "/%s/example/reported", nht_fb_uid());
        nht_fb_patch(path, "{\"alive\":true}");    /* queued; returns at once */
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
