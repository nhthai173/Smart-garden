/**
 * @file main.c
 * @brief nht_sched example: a daily task at 06:00 with a 2-byte payload.
 *
 * Without SNTP the clock is set by hand to 05:59:50 so the task fires after
 * ten seconds. On a real device, wait for nht_net_time_synced() instead.
 */
#include <sys/time.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nht_sched.h"

static const char *TAG = "example";

void app_main(void)
{
    /* Demo clock: 2026-01-01 05:59:50 UTC. */
    struct timeval tv = { .tv_sec = 1767247190, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    nht_sched_t sched;
    nht_sched_init(&sched, 0);                     /* default 10 min catch-up */

    uint16_t run_seconds = 900;                    /* the application payload */
    nht_sched_task_t t = {
        .hour = 6, .minute = 0,
        .repeat_mask = NHT_SCHED_REPEAT_DAILY,
        .enabled = true,
    };
    nht_sched_set_payload(&t, &run_seconds, sizeof run_seconds);
    uint8_t id;
    nht_sched_add(&sched, &t, &id);

    while (1) {
        nht_time_t now;
        nht_time_from_unix((int64_t)time(NULL), 0 /* UTC */, &now);

        nht_sched_task_t due;
        while (nht_sched_due(&sched, &now, &due)) {   /* collect everything due */
            uint16_t secs;
            memcpy(&secs, due.payload, sizeof secs);
            ESP_LOGI(TAG, "task %u is due: run for %u s", due.id, secs);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
