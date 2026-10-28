/**
 * @file main.c
 * @brief nht_net example: join WiFi, fall back to a setup AP, sync the clock.
 *
 * Edit WIFI_SSID / WIFI_PASS. With a wrong password the device raises the
 * "nht-example-setup" access point after 45 s.
 */
#include <stdio.h>
#include "nvs_flash.h"
#include "esp_log.h"
#include "nht_net.h"

#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"

static const char *TAG = "example";

/* Runs on every state change; it must be quick and must not block. */
static void on_state(nht_net_state_t st, void *user)
{
    static const char *names[] = { "idle", "connecting", "connected", "AP mode" };
    char ip[16] = "";
    nht_net_ip(ip, sizeof ip);
    ESP_LOGI(TAG, "network: %s %s (rssi %d)", names[st], ip, nht_net_rssi());
}

void app_main(void)
{
    nvs_flash_init();

    nht_net_cfg_t cfg = {
        .sta_ssid = WIFI_SSID, .sta_pass = WIFI_PASS,
        .ap_ssid = "nht-example-setup",
        .hostname = "nht-example",
        .tz_offset_s = 7 * 3600,
    };
    nht_net_on_state(on_state, NULL);
    nht_net_start(&cfg);              /* returns at once; joining is in the background */
}
