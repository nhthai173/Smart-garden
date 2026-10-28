/**
 * Smart Garden v2 - ESP32-C3 / ESP-IDF
 *
 * app_main chỉ làm đúng một việc: dựng các tầng theo thứ tự rồi rút lui.
 * Toàn bộ hành vi nằm trong các task, và task điều khiển (ctl) không phụ
 * thuộc bất kỳ thứ gì liên quan tới mạng.
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

#include "board_config.h"
#include "version.h"
#include "app_boot.h"
#include "app_cfg.h"
#include "app_water.h"
#include "app_http.h"
#include "app_cloud.h"
#if defined(SG_SIM)
#include "app_selftest.h"
#endif
#include "nht_net.h"
#if defined(NHT_ADC_FAKE)
#include "nht_adc.h"
#endif

static const char *TAG = "sg";

/** LED nhấp nháy theo trạng thái: chậm = bình thường, nhanh = đang tưới. */
static void led_task(void *arg)
{
    (void)arg;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_STATUS_LED,
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);

    bool on = false;
    while (1) {
        water_status_t st;
        water_get_status(&st);

        uint32_t period = 2000;
        if (st.leak)                             period = 150;   /* báo động */
        else if (st.valve != NHT_VALVE_CLOSED)   period = 400;   /* đang tưới */

        gpio_set_level(PIN_STATUS_LED,
                       on ? LED_ACTIVE_LEVEL : !LED_ACTIVE_LEVEL);
        on = !on;
        vTaskDelay(pdMS_TO_TICKS(period / 2));
    }
}

void app_main(void)
{
    app_boot_capture();

    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, " Smart Garden v2   fw %s (build %d)",
             FIRMWARE_VERSION, FIRMWARE_BUILD);
    ESP_LOGI(TAG, " Khởi động lại vì: %s", app_last_reset_str());
    ESP_LOGI(TAG, " Heap tự do: %u B", (unsigned)esp_get_free_heap_size());
    ESP_LOGI(TAG, "==============================================");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    app_cfg_load();

    /* Tầng tưới lên trước tầng mạng - có mạng hay không thì van vẫn phải
       về trạng thái an toàn và lịch vẫn phải chạy. */
#if defined(NHT_ADC_FAKE)
    /* Trình mô phỏng không có ngoại vi ADC. Đặt sẵn giá trị "khô" và "điện
       áp bình thường" để cảm biến không báo bừa ngay lúc khởi động. */
    nht_adc_fake_set(PIN_LEAK_ADC, 100);
    nht_adc_fake_set(PIN_VOLTAGE_ADC, 1500);   /* ~12.5 V qua cầu chia 11:1 */
#endif

    ESP_ERROR_CHECK(water_init());
    ESP_ERROR_CHECK(water_start());

    xTaskCreate(led_task, "led", 2560, NULL, 2, NULL);

#if defined(SG_SIM)
    /* QEMU không mô phỏng phần vô tuyến (esp_phy_enable sẽ assert), nên bản
       mô phỏng bỏ hẳn tầng mạng. Điều đó lại thành một phép thử tốt: nó
       chứng minh việc tưới chạy đủ khi không có mạng nào cả. */
    ESP_LOGW(TAG, "BẢN MÔ PHỎNG: bỏ qua WiFi/HTTP/Firebase, chạy self-test");
    app_selftest_start();
#else
    /* --- Tầng mạng: thuần adapter. Hỏng hay mất kết nối cũng không đụng
       tới việc tưới đã chạy ở trên. --- */
    app_cfg_t *c = app_cfg();

    char ap_ssid[CFG_STR_LEN + 16];
    snprintf(ap_ssid, sizeof(ap_ssid), "SmartGarden-%s", c->fb_device_id);

    nht_net_cfg_t nc = {
        .sta_ssid       = c->wifi_ssid,
        .sta_pass       = c->wifi_pass,
        .ap_ssid        = ap_ssid,
        .ap_pass        = NULL,          /* mạng mở: nhập cấu hình cho nhanh */
        .ap_fallback_ms = 45000,
        .hostname       = "smartgarden",
        .tz_offset_s    = c->tz_offset_s,
        .sntp_server    = "pool.ntp.org",
    };
    ESP_ERROR_CHECK(nht_net_start(&nc));

    /* Web server lên ngay, không đợi có IP: ở chế độ AP nó chính là trang
       cấu hình mà người dùng cần vào. */
    ESP_ERROR_CHECK(app_http_start());

    /* Firebase là tuỳ chọn: chưa cấu hình, hoặc không đăng nhập được, thì
       thiết bị vẫn chạy đủ chức năng qua LAN. */
    app_cloud_start();
#endif

    /* Firmware mới sau OTA: chạy được tới đây nghĩa là ổn, chốt lại để
       bootloader không quay về bản cũ ở lần khởi động sau. */
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(run, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "firmware mới chạy ổn định, chốt lại");
        esp_ota_mark_app_valid_cancel_rollback();
    }

    ESP_LOGI(TAG, "khởi động xong, heap còn %u B",
             (unsigned)esp_get_free_heap_size());
}
