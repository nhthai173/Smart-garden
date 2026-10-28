/**
 * @file    main.c
 * @brief   Smart Garden v2 - ESP32-C3 / ESP-IDF entry point.
 *
 * app_main does exactly one thing: bring up the layers in order, then retire.
 * All behaviour lives in tasks, and the control task (ctl) depends on nothing
 * that has to do with the network.
 *
 * Layers, bottom to top (dependencies only point downwards):
 *   nhtlib   reusable components (valve, actuator, scheduler, WiFi, ...)
 *   sg_core  watering logic, safety, settings  - knows nothing about the network
 *   sg_net   web server and Firebase adapters  - sits on top of sg_core
 *   main     this file: wires the layers together
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "board_config.h"   /* pin map of the board                           */
#include "version.h"        /* firmware version, generated at build time      */
#include "app_boot.h"       /* why did we reset?                              */
#include "app_cfg.h"        /* settings stored in NVS                         */
#include "app_water.h"      /* watering logic: the source of truth            */
#include "app_http.h"       /* local web API (network layer)                  */
#include "app_cloud.h"      /* Firebase adapter (network layer)               */
#if defined(SG_SIM)
#include "app_selftest.h"   /* on-target self-test, simulator build only      */
#endif
#include "nht_net.h"        /* WiFi / SNTP                                    */
#if defined(NHT_ADC_FAKE)
#include "nht_adc.h"        /* fake ADC values for simulators / bare boards   */
#endif

static const char *TAG = "sg";

/**
 * Status LED: slow blink = idle, fast = watering, very fast = leak alarm.
 * It only reads the status; it never touches the valve.
 */
static void led_task(void *arg)
{
    (void)arg;

    /* Configure the on-board LED pin as a plain output. */
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_STATUS_LED,
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);

    bool on = false;
    while (1) {
        /* Snapshot the current watering status (thread-safe). */
        water_status_t st;
        water_get_status(&st);

        /* Pick the blink period from the status; idle by default. */
        uint32_t period = 2000;
        if (st.leak)                             period = 150;   /* alarm    */
        else if (st.valve != NHT_VALVE_CLOSED)   period = 400;   /* watering */

        /* Toggle the LED (the on-board LED is active-low). */
        gpio_set_level(PIN_STATUS_LED,
                       on ? LED_ACTIVE_LEVEL : !LED_ACTIVE_LEVEL);
        on = !on;

        /* Half a period per toggle. */
        vTaskDelay(pdMS_TO_TICKS(period / 2));
    }
}

void app_main(void)
{
    /* First thing: remember why we reset (power-on, crash, watchdog...). */
    app_boot_capture();

    /* Startup banner; the rig that tests the board looks for this line. */
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, " Smart Garden v2   fw %s (build %d)",
             FIRMWARE_VERSION, FIRMWARE_BUILD);
    ESP_LOGI(TAG, " Khởi động lại vì: %s", app_last_reset_str());
    ESP_LOGI(TAG, " Heap tự do: %u B", (unsigned)esp_get_free_heap_size());
    ESP_LOGI(TAG, "==============================================");

    /* Bring up flash storage. A full or newer-format partition is erased and
       re-initialised rather than leaving the device unable to boot. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* Load settings from NVS; anything missing keeps its built-in default. */
    app_cfg_load();

    /* The watering layer comes up BEFORE the network: with or without a
       network the valve must reach its safe state and the schedule must run. */
#if defined(NHT_ADC_FAKE)
    /* A simulator (or a board with nothing wired) has no real ADC input. Preset
       a "dry" probe and a "normal supply voltage" so the safety logic does not
       raise a false alarm right at start-up. */
    nht_adc_fake_set(PIN_LEAK_ADC, 100);       /* dry probe                    */
    nht_adc_fake_set(PIN_VOLTAGE_ADC, 1500);   /* ~12.5 V through the 11:1 divider */
#endif

    /* Create the watering logic: drives the valve to CLOSED, restores the
       saved schedule, and sets up the sensors. */
    ESP_ERROR_CHECK(water_init());
    /* Start the event-driven control task and its sampling timers. */
    ESP_ERROR_CHECK(water_start());

    /* Blink the status LED. */
    xTaskCreate(led_task, "led", 2560, NULL, 2, NULL);

#if defined(SG_SIM)
    /* QEMU emulates no radio (esp_phy_enable would assert), so the simulator
       build skips the network layer entirely. That doubles as a good test: it
       proves watering works with no network at all. */
    ESP_LOGW(TAG, "BẢN MÔ PHỎNG: bỏ qua WiFi/HTTP/Firebase, chạy self-test");
    app_selftest_start();
#else
    /* --- Network layer: pure adapters. A failure or an outage here never
       touches the watering that is already running above. --- */
    app_cfg_t *c = app_cfg();

    /* Name of the setup access point, unique per device. */
    char ap_ssid[CFG_STR_LEN + 16];
    snprintf(ap_ssid, sizeof(ap_ssid), "SmartGarden-%s", c->fb_device_id);

    /* WiFi settings: join the saved network, fall back to a setup AP. */
    nht_net_cfg_t nc = {
        .sta_ssid       = c->wifi_ssid,
        .sta_pass       = c->wifi_pass,
        .ap_ssid        = ap_ssid,
        .ap_pass        = NULL,          /* open network: quick to configure  */
        .ap_fallback_ms = 45000,         /* try the station for 45 s first    */
        .hostname       = "smartgarden",
        .tz_offset_s    = c->tz_offset_s,
        .sntp_server    = "pool.ntp.org",
    };
    /* Returns at once; joining the network happens in the background. */
    ESP_ERROR_CHECK(nht_net_start(&nc));

    /* Start the web server immediately, without waiting for an IP: in AP mode
       it is the very setup page the user needs. */
    ESP_ERROR_CHECK(app_http_start());

    /* Firebase is optional: if it is not configured, or sign-in fails, the
       device keeps working fully over the LAN. */
    app_cloud_start();
#endif

    /* After an OTA update, reaching this point means the new firmware is
       healthy: confirm it, or the bootloader would roll back to the old image
       on the next boot. */
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(run, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "firmware mới chạy ổn định, chốt lại");
        esp_ota_mark_app_valid_cancel_rollback();
    }

    /* app_main returns; everything continues in the tasks created above. */
    ESP_LOGI(TAG, "khởi động xong, heap còn %u B",
             (unsigned)esp_get_free_heap_size());
}
