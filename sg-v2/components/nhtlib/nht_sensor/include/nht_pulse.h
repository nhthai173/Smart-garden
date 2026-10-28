/**
 * @file    nht_pulse.h
 * @brief   Pulse counting for flow meters, energy meters and one-way encoders.
 * @version 1.0.0
 * @license MIT
 *
 * Two backends, chosen automatically per chip, behind one API:
 *
 *  - Chips with a PCNT peripheral (ESP32, S2, S3, C6, H2): counted in hardware.
 *    The PCNT counter is only 16 bits wide, so the library accumulates each
 *    overflow through a watch point; callers see a 64-bit counter that never
 *    wraps. Pulses are not lost even while the CPU is busy.
 *
 *  - Chips without PCNT (ESP32-C3, C2): counted in a GPIO interrupt with a
 *    software glitch filter. That is plenty for a flow meter - a YF-S201 at
 *    30 L/min is only about 225 Hz - but not suitable for tens of kHz.
 *
 * @par Example
 * @code
 * nht_pulse_t *flow;
 * nht_pulse_cfg_t cfg = { .gpio = 0, .pulses_per_unit = 450.0f, .pullup = true };
 * nht_pulse_init(&flow, &cfg);
 * // every few seconds:
 * float lpm   = nht_pulse_rate(flow, now_ms());   // litres per minute
 * float total = nht_pulse_volume(flow);           // litres
 * @endcode
 */

#ifndef NHT_PULSE_H
#define NHT_PULSE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Pulse counter configuration. */
typedef struct {
    int      gpio;
    uint16_t glitch_ns;        /**< ignore pulses shorter than this; 0 -> 1000 ns */
    float    pulses_per_unit;  /**< for example pulses per litre; 0 -> 1          */
    bool     pullup;           /**< enable the internal pull-up                   */
} nht_pulse_cfg_t;

/** Opaque counter handle. */
typedef struct nht_pulse_s nht_pulse_t;

/**
 * Create a counter and start counting.
 *
 * @param[out] out handle
 * @param cfg      configuration
 * @return ESP_OK or an ESP-IDF error
 */
esp_err_t nht_pulse_init(nht_pulse_t **out, const nht_pulse_cfg_t *cfg);

/** Stop counting and free the handle. */
void      nht_pulse_deinit(nht_pulse_t *p);

/** @return total pulses since the last reset. */
uint64_t  nht_pulse_total(nht_pulse_t *p);

/** @return total in physical units (litres...) = pulses / pulses_per_unit. */
float     nht_pulse_volume(nht_pulse_t *p);

/**
 * Average rate since the previous call, in units per minute.
 * Call it regularly (every 1-5 s). The first call returns 0.
 *
 * @param p      counter
 * @param now_ms monotonic time in milliseconds
 */
float     nht_pulse_rate(nht_pulse_t *p, uint32_t now_ms);

/** Zero the counter. */
void      nht_pulse_reset(nht_pulse_t *p);

#ifdef __cplusplus
}
#endif
#endif /* NHT_PULSE_H */
