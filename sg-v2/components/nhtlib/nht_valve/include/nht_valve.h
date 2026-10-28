/**
 * @file    nht_valve.h
 * @brief   Water-valve control: motorised ball valve and normally-closed
 *          solenoid valve behind one API.
 * @version 1.1.0
 * @license MIT
 *
 * Wraps the open/close sequences of the two valve types common in irrigation
 * systems. Switching valve type is one field in the configuration; the code
 * above this layer does not change.
 *
 *  - @ref NHT_VALVE_SOLENOID: normally-closed solenoid, one relay. Power opens
 *    it, a spring closes it. Losing power closes it - safe for free.
 *  - @ref NHT_VALVE_BALL: motorised ball valve. The motor needs several seconds
 *    to travel end to end, so it needs a power pin plus a direction pin. A
 *    partial opening is made by powering the motor for part of its travel.
 *
 * Optional extras: a pump relay (switched on after the valve starts opening and
 * off before it closes) and a master AC relay (cut a while after the valve
 * stops moving).
 *
 * @par Safety
 *  - `max_run_ms` is a hard ceiling. No command, schedule or cloud request can
 *    exceed it; it is the last line of defence if the layer above miscomputes.
 *  - nht_valve_init() always *closes* the valve. For a ball valve that means a
 *    full closing run, not just releasing a relay: after a reset in the middle
 *    of watering nobody knows where the valve is.
 *  - Reversing the motor mid-travel never switches the direction relay while the
 *    motor is powered: power is cut, the valve waits `reverse_settle_ms`, then
 *    the direction changes and a *full* closing (or opening) travel is run.
 *  - The safety inhibit closes the valve immediately and rejects every open
 *    command until cleared.
 *
 * @par Time and event-driven use
 * Time is passed in as @p now_ms; no function reads a clock, so a whole
 * watering cycle can be unit-tested on a PC. Instead of ticking at a fixed
 * rate, sleep for nht_valve_next_wake_ms() and then call nht_valve_tick().
 *
 * @par Example
 * @code
 * nht_valve_t valve;
 * nht_valve_cfg_t cfg = {
 *     .type = NHT_VALVE_BALL, .pin_power = 5, .pin_dir = 6,
 *     .travel_ms = 8000, .max_run_ms = 30 * 60 * 1000,
 * };
 * nht_valve_init(&valve, &cfg, now_ms());            // closes the valve
 * nht_valve_open(&valve, 5 * 60 * 1000, 100, now_ms());
 * uint32_t wait;
 * if (nht_valve_next_wake_ms(&valve, now_ms(), &wait)) { sleep_ms(wait); }
 * nht_valve_tick(&valve, now_ms());
 * @endcode
 */

#ifndef NHT_VALVE_H
#define NHT_VALVE_H

#include <stdint.h>
#include <stdbool.h>
#include "nht_actuator.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Valve hardware type. */
typedef enum {
    NHT_VALVE_SOLENOID = 0,
    NHT_VALVE_BALL     = 1,
} nht_valve_type_t;

/** Valve state. */
typedef enum {
    NHT_VALVE_CLOSED = 0,
    NHT_VALVE_OPENING,
    NHT_VALVE_OPEN,
    NHT_VALVE_CLOSING,
} nht_valve_state_t;

/** Result of a command. */
typedef enum {
    NHT_VALVE_OK = 0,
    NHT_VALVE_ERR_INHIBIT,
    NHT_VALVE_ERR_BUSY,     /**< motor is closing; wait for it to finish */
    NHT_VALVE_ERR_PARAM,
} nht_valve_err_t;

/** Event reported to the callback set with nht_valve_on_event(). */
typedef enum {
    NHT_VALVE_EV_OPENED = 0,
    NHT_VALVE_EV_CLOSED,
    NHT_VALVE_EV_AUTO_CLOSED,  /**< closed because the duration ran out */
    NHT_VALVE_EV_INHIBITED,    /**< closed by the safety inhibit        */
} nht_valve_event_t;

struct nht_valve_s;
/** Valve handle; the struct is public only so it can live on the stack. */
typedef struct nht_valve_s nht_valve_t;

/**
 * Valve event callback.
 *
 * @param v    the valve
 * @param ev   what happened
 * @param user pointer given to nht_valve_on_event()
 */
typedef void (*nht_valve_evt_cb_t)(nht_valve_t *v, nht_valve_event_t ev,
                                   void *user);

/** Valve configuration. Pins set to -1 are not used. */
typedef struct {
    nht_valve_type_t type;

    int  pin_power;        /**< ball: motor supply. solenoid: coil          */
    int  pin_dir;          /**< ball: direction (on = close). -1 = none     */
    int  pin_pump;         /**< pump relay, -1 = none                       */
    int  pin_ac;           /**< master AC relay, -1 = none                  */
    bool active_high;      /**< false = active-low relay board              */

    uint32_t travel_ms;       /**< ball: time to travel end to end          */
    uint32_t pump_delay_ms;   /**< pump starts this long after the valve    */
    uint32_t ac_auto_off_ms;  /**< AC cut this long after the last motion;
                                   0 = never                                */
    uint32_t default_run_ms;  /**< run time when none is given              */
    uint32_t max_run_ms;      /**< HARD CEILING no command can exceed       */
    uint32_t reverse_settle_ms; /**< pause between cutting motor power and
                                     reversing direction; 0 = 100 ms        */

    nht_act_write_t write;    /**< pin-write hook; NULL = ESP-IDF GPIO      */
    void           *user;     /**< passed to @ref write                     */
} nht_valve_cfg_t;

/** Runtime state. Treat as read-only; use the API to change it. */
struct nht_valve_s {
    nht_valve_cfg_t cfg;
    nht_valve_state_t state;

    nht_act_t run;    /**< virtual output holding the watering time + auto-off */
    nht_act_t power;
    nht_act_t dir;
    nht_act_t pump;
    nht_act_t ac;

    uint32_t now_ms;          /**< time of the command being processed      */
    uint32_t travel_until_ms;
    bool     travelling;
    uint8_t  level_pct;
    bool     inhibit;

    /* A reversal in mid-travel: cut power, wait, then change direction. */
    bool     move_pending;
    bool     move_close;
    uint32_t move_ms;
    uint32_t move_at_ms;

    nht_valve_evt_cb_t on_event;
    void              *evt_user;
};

/**
 * Prepare the valve and drive it to a certainly-closed state.
 * A ball valve runs a full closing travel.
 *
 * @param v      valve to initialise
 * @param cfg    configuration (copied)
 * @param now_ms current time in milliseconds
 * @return NHT_VALVE_OK or NHT_VALVE_ERR_PARAM
 */
nht_valve_err_t nht_valve_init(nht_valve_t *v, const nht_valve_cfg_t *cfg,
                               uint32_t now_ms);

/**
 * Register the event callback (one per valve).
 *
 * @param v    valve
 * @param cb   callback, or NULL to remove
 * @param user opaque pointer handed back to the callback
 */
void nht_valve_on_event(nht_valve_t *v, nht_valve_evt_cb_t cb, void *user);

/**
 * Open the valve for @p run_ms milliseconds.
 *
 * If the valve is already open, only the duration is reset and the motor does
 * not run again, so repeating a call with the same arguments is harmless.
 *
 * @param v         valve
 * @param run_ms    run time; 0 = default_run_ms. Always clamped to max_run_ms
 * @param level_pct opening, 1..100, ball valve only; 0 = 100
 * @param now_ms    current time
 * @return NHT_VALVE_OK, NHT_VALVE_ERR_INHIBIT (safety inhibit set) or
 *         NHT_VALVE_ERR_BUSY (motor is closing; try again shortly)
 */
nht_valve_err_t nht_valve_open(nht_valve_t *v, uint32_t run_ms,
                               uint8_t level_pct, uint32_t now_ms);

/**
 * Close the valve. Safe to call in any state, including while it is opening
 * (the motor reverses with a full closing travel).
 *
 * @return NHT_VALVE_OK or NHT_VALVE_ERR_PARAM
 */
nht_valve_err_t nht_valve_close(nht_valve_t *v, uint32_t now_ms);

/**
 * Close the valve regardless of any constraint. Use for faults.
 *
 * @return NHT_VALVE_OK or NHT_VALVE_ERR_PARAM
 */
nht_valve_err_t nht_valve_close_force(nht_valve_t *v, uint32_t now_ms);

/**
 * Set or clear the safety inhibit. Setting it closes the valve at once and
 * rejects every open command until cleared.
 */
void nht_valve_set_inhibit(nht_valve_t *v, bool inhibit, uint32_t now_ms);

/**
 * Advance the valve state machine (end of travel, end of run time, pump/AC
 * timers). Call it when nht_valve_next_wake_ms() elapses and after each
 * command. Calling it early is harmless.
 */
void nht_valve_tick(nht_valve_t *v, uint32_t now_ms);

/**
 * How long until nht_valve_tick() has something to do: end of travel, end of
 * watering time, pump or AC timers, end of a reversal pause.
 *
 * @param[out] out_ms milliseconds from @p now_ms (0 = due now)
 * @return false if the valve is idle and any sleep length is fine
 */
bool nht_valve_next_wake_ms(const nht_valve_t *v, uint32_t now_ms,
                            uint32_t *out_ms);

/**
 * Milliseconds of watering time left.
 *
 * @return 0 if closed or without a deadline
 */
uint32_t nht_valve_remaining_ms(const nht_valve_t *v, uint32_t now_ms);

/** Human-readable state name ("closed", "opening", "open", "closing"). */
const char *nht_valve_state_str(nht_valve_state_t s);

/** @return the current valve state. */
static inline nht_valve_state_t nht_valve_state(const nht_valve_t *v)
{
    return v->state;
}

/** @return true while the valve is open or travelling towards open. */
static inline bool nht_valve_is_flowing(const nht_valve_t *v)
{
    return v->state == NHT_VALVE_OPEN || v->state == NHT_VALVE_OPENING;
}

#ifdef __cplusplus
}
#endif
#endif /* NHT_VALVE_H */
