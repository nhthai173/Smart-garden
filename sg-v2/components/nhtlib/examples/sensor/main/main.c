/**
 * @file main.c
 * @brief nht_sensor example: read a voltage on an ADC pin and count flow-meter
 *        pulses on another pin.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nht_adc.h"
#include "nht_pulse.h"

static const char *TAG = "example";

void app_main(void)
{
    /* 100k / 10k divider on the pin: scale 11 turns the pin voltage into volts. */
    nht_adc_t vin;
    nht_adc_cfg_t acfg = { .gpio = 4, .samples = 5, .scale = 11.0f };
    esp_err_t adc_ok = nht_adc_init(&vin, &acfg);

    /* YF-S201 style flow meter: about 450 pulses per litre. */
    nht_pulse_t *flow = NULL;
    nht_pulse_cfg_t pcfg = { .gpio = 0, .pulses_per_unit = 450.0f, .pullup = true };
    nht_pulse_init(&flow, &pcfg);

    while (1) {
        float volts = 0;
        if (adc_ok == ESP_OK && nht_adc_read_value(&vin, &volts) == ESP_OK)
            ESP_LOGI(TAG, "supply: %.2f V", volts);

        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        ESP_LOGI(TAG, "flow: %.2f L/min, total %.2f L",
                 nht_pulse_rate(flow, now), nht_pulse_volume(flow));
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
