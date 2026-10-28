/**
 * @file main.c
 * @brief nht_actuator example: a relay that switches itself off after 3 s.
 *
 * Shows the event-driven pattern: instead of ticking at a fixed rate, the task
 * sleeps for exactly as long as nht_act_next_wake_ms() says, then ticks.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nht_actuator.h"

#define RELAY_PIN 5

static const char *TAG = "example";

/* Milliseconds since boot, the time base every nht_* call expects. */
static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* Called whenever the relay really switches or is switched off. */
static void on_event(nht_act_t *a, nht_act_event_t ev, void *user)
{
    static const char *names[] = { "ON", "OFF", "AUTO_OFF", "INHIBITED" };
    ESP_LOGI(TAG, "relay event: %s", names[ev]);
}

void app_main(void)
{
    nht_act_t relay;
    nht_act_cfg_t cfg = {
        .pin = RELAY_PIN,
        .active_high = false,        /* active-low relay board              */
        .auto_off_ms = 3000,         /* default run time                    */
    };
    nht_act_init(&relay, &cfg, now_ms());
    nht_act_on_event(&relay, on_event, NULL);

    nht_act_on(&relay, now_ms());    /* switches on, will switch off by itself */

    while (1) {
        uint32_t wait;
        /* Sleep until the next deadline; 1 s when nothing is pending. */
        if (!nht_act_next_wake_ms(&relay, now_ms(), &wait)) wait = 1000;
        vTaskDelay(pdMS_TO_TICKS(wait) + 1);
        nht_act_tick(&relay, now_ms());   /* fires the auto-off when due */
    }
}
