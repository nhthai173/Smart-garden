/**
 * @file    nht_dinput.h
 * @brief   Noise-tolerant discrete input: Schmitt trigger plus asymmetric
 *          hold times.
 * @version 1.0.0
 * @license MIT
 *
 * For any two-state sensor whose real signal hovers around a threshold: water
 * leak probes, float switches, soil moisture, rain sensors, door contacts,
 * photoresistors. Analog and digital readings share one API.
 *
 * @par Why two layers
 * A plain symmetric debounce (hold N ms in both directions) cannot save a damp
 * sensor. The signal wobbles around a *single* threshold and now and then
 * stays on one side long enough to pass the debounce, producing a chain of
 * false transitions that can go on for hours.
 *
 *  - Layer 1, Schmitt trigger (analog only): two thresholds instead of one.
 *    While inactive the value must exceed `thr_on`; while active it must fall
 *    below `thr_off`. The band between them produces no transition at all.
 *  - Layer 2, asymmetric hold: `hold_on_ms` and `hold_off_ms` may differ. For
 *    a safety alarm, enter fast and leave slowly (for example 5 s and 10 min):
 *    while it is still damp, treat it as still faulty.
 *
 * Digital mode has only layer 2 - that is all that can be done once a
 * comparator module has thrown the magnitude away.
 *
 * Everything here is pure logic: time is passed in, nothing calls millis() or
 * esp_timer, so a 3-hour scenario can be unit-tested in 1 ms.
 *
 * @par Example
 * @code
 * nht_dinput_t leak = {
 *     .analog = true, .thr_on = 2400, .thr_off = 1200,
 *     .hold_on_ms = 5000, .hold_off_ms = 600000,
 *     .on_change = on_leak_change,
 * };
 * nht_dinput_init(&leak, NHT_DIN_INACTIVE);
 * // every 200 ms:
 * nht_dinput_feed(&leak, adc_raw, now_ms());
 * @endcode
 */

#ifndef NHT_DINPUT_H
#define NHT_DINPUT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Debounced state. */
typedef enum {
    NHT_DIN_INACTIVE = 0,
    NHT_DIN_ACTIVE   = 1,
} nht_din_state_t;

struct nht_dinput_s;

/**
 * Called when the *accepted* state changes.
 *
 * @param in   the input that changed
 * @param st   the new state
 * @param user pointer from nht_dinput_t::user
 */
typedef void (*nht_din_cb_t)(struct nht_dinput_s *in, nht_din_state_t st,
                             void *user);

/** Input configuration and state. Fill the configuration fields, then init. */
typedef struct nht_dinput_s {
    /* ---- configuration ---- */
    bool     analog;       /**< true: `raw` is an ADC value; false: 0/1 level   */
    bool     inverted;     /**< true if the circuit reads LOW when active       */
    uint16_t thr_on;       /**< threshold to enter ACTIVE (analog only)         */
    uint16_t thr_off;      /**< threshold to leave ACTIVE (analog, < thr_on)    */
    uint16_t adc_max;      /**< ADC full scale, default 4095                    */
    uint32_t hold_on_ms;   /**< must stay ACTIVE this long to be accepted       */
    uint32_t hold_off_ms;  /**< must stay INACTIVE this long to be accepted     */

    nht_din_cb_t on_change; /**< optional change callback                       */
    void        *user;      /**< passed to the callback                         */

    /* ---- state (read-only) ---- */
    nht_din_state_t state;
    nht_din_state_t pending;
    bool     has_pending;
    uint32_t pending_since_ms;
    uint32_t changed_at_ms;
    uint32_t transitions;  /**< total accepted state changes so far             */
    uint16_t last_raw;     /**< most recent sample                              */
} nht_dinput_t;

/** Default ADC full scale. */
#define NHT_DIN_DEFAULT_ADC_MAX   4095u
/** Default hold time when none is configured. */
#define NHT_DIN_DEFAULT_HOLD_MS     50u

/**
 * Normalise the configuration and set the initial state.
 *
 * Fill the configuration fields *before* calling. They are kept as they are;
 * only blanks get defaults, and thr_off < thr_on is enforced.
 *
 * @param in      input to initialise
 * @param initial state assumed at start-up
 */
void nht_dinput_init(nht_dinput_t *in, nht_din_state_t initial);

/**
 * Feed one sample.
 *
 * @param in     input
 * @param raw    ADC value (analog) or logic level 0/1 (digital)
 * @param now_ms monotonic time in milliseconds
 * @return true if the accepted state changed on exactly this call (the
 *         on_change callback, if any, has already run)
 */
bool nht_dinput_feed(nht_dinput_t *in, uint16_t raw, uint32_t now_ms);

/**
 * How long the input has been in its current state.
 *
 * @return milliseconds
 */
uint32_t nht_dinput_stable_ms(const nht_dinput_t *in, uint32_t now_ms);

/** @return true while the accepted state is ACTIVE. */
static inline bool nht_dinput_active(const nht_dinput_t *in)
{
    return in->state == NHT_DIN_ACTIVE;
}

/**
 * Median of a sample array - removes noise spikes before feeding the input.
 * Sorts in place (the buffer is modified). An odd @p n gives the cleanest
 * result.
 *
 * @param buf samples
 * @param n   number of samples
 * @return the median value (0 if @p n is 0)
 */
uint16_t nht_median_u16(uint16_t *buf, uint32_t n);

#ifdef __cplusplus
}
#endif
#endif /* NHT_DINPUT_H */
