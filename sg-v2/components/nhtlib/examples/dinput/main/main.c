/**
 * @file main.c
 * @brief nht_dinput example: a debounced digital input (for example a float
 *        switch) that must stay active for 2 s and inactive for 5 s.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nht_dinput.h"

#define SENSOR_PIN GPIO_NUM_1

static const char *TAG = "example";

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* Called only when the ACCEPTED state changes, never for bounce or noise. */
static void on_change(nht_dinput_t *in, nht_din_state_t st, void *user)
{
    ESP_LOGI(TAG, "sensor is now %s", st == NHT_DIN_ACTIVE ? "ACTIVE" : "inactive");
}

void app_main(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << SENSOR_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);

    nht_dinput_t in = {
        .analog = false,
        .inverted = true,            /* the switch pulls the pin LOW when active */
        .hold_on_ms = 2000,
        .hold_off_ms = 5000,
        .on_change = on_change,
    };
    nht_dinput_init(&in, NHT_DIN_INACTIVE);

    while (1) {
        nht_dinput_feed(&in, gpio_get_level(SENSOR_PIN), now_ms());
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
