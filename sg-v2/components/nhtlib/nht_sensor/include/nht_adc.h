/**
 * @file    nht_adc.h
 * @brief   One-shot ADC reading with calibration and median filtering.
 * @version 1.0.0
 * @license MIT
 *
 * A thin wrapper over the ESP-IDF `adc_oneshot` and `adc_cali` drivers. It adds
 * median filtering (noise spikes are discarded instead of being averaged into
 * the result) and a linear conversion to a physical unit.
 *
 * The GPIO to ADC channel mapping is looked up internally, so callers only need
 * the pin number from the schematic.
 *
 * @par Example
 * @code
 * nht_adc_t vin;
 * nht_adc_cfg_t cfg = { .gpio = 4, .samples = 5, .scale = 11.0f };  // 100k/10k divider
 * nht_adc_init(&vin, &cfg);
 * float volts;
 * if (nht_adc_read_value(&vin, &volts) == ESP_OK) printf("%.2f V\n", volts);
 * @endcode
 */

#ifndef NHT_ADC_H
#define NHT_ADC_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"

#ifdef __cplusplus
extern "C" {
#endif

/** ADC channel configuration. */
typedef struct {
    int         gpio;     /**< GPIO number of the analog pin                     */
    adc_atten_t atten;    /**< default ADC_ATTEN_DB_12 (widest range)            */
    uint8_t     samples;  /**< samples per median; 0 -> 5. Use an odd number     */
    float       scale;    /**< multiplies the voltage (V) to get the physical unit */
    float       offset;   /**< added after scaling                               */
} nht_adc_cfg_t;

/** ADC channel handle. */
typedef struct {
    nht_adc_cfg_t        cfg;
    adc_oneshot_unit_handle_t unit;
    adc_cali_handle_t    cali;
    adc_unit_t           unit_id;
    adc_channel_t        channel;
    bool                 calibrated;
    bool                 owns_unit;
} nht_adc_t;

/** Upper limit for nht_adc_cfg_t::samples. */
#define NHT_ADC_MAX_SAMPLES 9

/**
 * Initialise a channel.
 *
 * Several sensors sharing one ADC unit is normal: the library shares the
 * hardware handle between them with reference counting. ESP-IDF allows only
 * one handle per unit, so without this the second sensor would be refused with
 * "adc1 is already in use".
 *
 * @return ESP_OK or an ESP-IDF error (for example the pin is not an ADC pin)
 */
esp_err_t nht_adc_init(nht_adc_t *a, const nht_adc_cfg_t *cfg);

/** Release the channel (and the shared unit when this was the last user). */
void      nht_adc_deinit(nht_adc_t *a);

/**
 * Raw reading, 0..4095, after median filtering.
 *
 * @param[out] raw result
 * @return ESP_OK or an error (nothing is written to @p raw on error)
 */
esp_err_t nht_adc_read_raw(nht_adc_t *a, uint16_t *raw);

/**
 * Voltage at the pin in millivolts. If the chip cannot be calibrated, a linear
 * estimate for the configured attenuation is used.
 *
 * @param[out] mv result
 */
esp_err_t nht_adc_read_mv(nht_adc_t *a, int *mv);

/**
 * Physical value: `(mV / 1000) * scale + offset`.
 *
 * @param[out] out result
 */
esp_err_t nht_adc_read_value(nht_adc_t *a, float *out);

#if defined(NHT_ADC_FAKE)
/**
 * Only when built with `-D NHT_ADC_FAKE`: set a fake raw value for a pin, for
 * simulators and for a board with nothing wired to it.
 *
 * @param gpio GPIO number
 * @param v    raw value returned by nht_adc_read_raw()
 */
void nht_adc_fake_set(int gpio, uint16_t v);
#endif

#ifdef __cplusplus
}
#endif
#endif /* NHT_ADC_H */
