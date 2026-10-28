/**
 * @file main.c
 * @brief nht_valve example: open a motorised ball valve for 20 s.
 *
 * The valve is driven to a certainly-closed state at start-up, then opened for a
 * fixed time. The loop sleeps until the valve next needs attention.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nht_valve.h"

static const char *TAG = "example";

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* Report what the valve does. */
static void on_event(nht_valve_t *v, nht_valve_event_t ev, void *user)
{
    static const char *names[] = { "OPENED", "CLOSED", "AUTO_CLOSED", "INHIBITED" };
    ESP_LOGI(TAG, "valve event: %s", names[ev]);
}

void app_main(void)
{
    nht_valve_t valve;
    nht_valve_cfg_t cfg = {
        .type       = NHT_VALVE_BALL,
        .pin_power  = 5,              /* motor supply relay                   */
        .pin_dir    = 6,              /* direction relay (on = close)         */
        .pin_pump   = -1,             /* no pump relay                        */
        .pin_ac     = -1,             /* no master AC relay                   */
        .active_high = false,
        .travel_ms  = 8000,           /* end-to-end travel time               */
        .max_run_ms = 30 * 60 * 1000, /* hard ceiling: 30 minutes             */
    };
    nht_valve_init(&valve, &cfg, now_ms());      /* closes the valve fully    */
    nht_valve_on_event(&valve, on_event, NULL);

    /* Open 100% for 20 s. A repeated identical call would change nothing. */
    nht_valve_open(&valve, 20 * 1000, 100, now_ms());

    while (1) {
        uint32_t wait;
        if (!nht_valve_next_wake_ms(&valve, now_ms(), &wait)) wait = 1000;
        vTaskDelay(pdMS_TO_TICKS(wait) + 1);
        nht_valve_tick(&valve, now_ms());
    }
}
