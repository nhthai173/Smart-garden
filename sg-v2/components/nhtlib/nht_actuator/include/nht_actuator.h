/**
 * @file    nht_actuator.h
 * @brief   Stateful output: auto-off timer, turn-on delay, anti-chatter and a
 *          safety inhibit. A relay / MOSFET / virtual-output controller.
 * @version 1.1.0
 * @license MIT
 *
 * One @ref nht_act_t owns one output. Writing the physical level goes through
 * a hook, so the same code drives a GPIO, a port expander (PCF8574), a shift
 * register, or a plain variable in a unit test.
 *
 * All time is passed in as @p now_ms; no function reads a clock. In return for
 * the extra argument, an entire multi-hour schedule can be unit-tested in a few
 * milliseconds and nothing can change behind your back.
 *
 * @par Semantics of nht_act_on_for() - read this carefully
 * This is the easiest thing to get wrong, and an earlier Arduino implementation
 * did: calling "on" while the output was already on cleared the one-shot
 * duration and moved the timer origin to "now" *before* the early return for
 * "already on". A single repeated command (for example a sync event echoing
 * back) silently turned a 15-minute run into the 1-minute default.
 *
 * Here, while the output is on:
 *  - nht_act_on_for() only *resets the deadline* to `turn_on_time + ms`. It does
 *    not rerun the turn-on sequence or move the turn-on time. Repeating the same
 *    command therefore changes nothing: duplicate commands are harmless by
 *    design.
 *  - nht_act_on() never touches a one-shot duration that is running.
 *  - To add time measured from now, call nht_act_extend().
 *
 * @par Event-driven use
 * Instead of ticking at a fixed rate, sleep for nht_act_next_wake_ms() and call
 * nht_act_tick() when it elapses (and after every command).
 *
 * @par Example
 * @code
 * nht_act_t relay;
 * nht_act_cfg_t cfg = { .pin = 5, .active_high = false, .auto_off_ms = 60000 };
 * nht_act_init(&relay, &cfg, now_ms());
 * nht_act_on_for(&relay, 15 * 60 * 1000, now_ms());   // run for 15 minutes
 * // ... later, from the main loop:
 * nht_act_tick(&relay, now_ms());                      // fires the auto-off
 * @endcode
 */

#ifndef NHT_ACTUATOR_H
#define NHT_ACTUATOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Output state. */
typedef enum {
    NHT_ACT_OFF = 0,
    NHT_ACT_PENDING_ON,   /**< turn-on command accepted, waiting out on_delay_ms */
    NHT_ACT_ON,
} nht_act_state_t;

/** Result of a command. */
typedef enum {
    NHT_ACT_OK = 0,
    NHT_ACT_ERR_INHIBIT,  /**< the safety inhibit is set                        */
    NHT_ACT_ERR_MIN_OFF,  /**< switched off too recently (min_off_ms)           */
    NHT_ACT_ERR_MIN_ON,   /**< switched on too recently (min_on_ms)             */
    NHT_ACT_ERR_PARAM,    /**< invalid argument                                 */
} nht_act_err_t;

/** Event reported to the callback set with nht_act_on_event(). */
typedef enum {
    NHT_ACT_EV_ON = 0,    /**< the circuit is now really closed (after on_delay) */
    NHT_ACT_EV_OFF,       /**< switched off by command                           */
    NHT_ACT_EV_AUTO_OFF,  /**< switched off because the duration ran out         */
    NHT_ACT_EV_INHIBITED, /**< forced off by the safety inhibit                  */
} nht_act_event_t;

/** Level of the output right after nht_act_init(). */
typedef enum {
    NHT_ACT_START_OFF = 0,
    NHT_ACT_START_ON,
} nht_act_startup_t;

struct nht_act_s;
/** Opaque-ish handle; the struct is public only so it can live on the stack. */
typedef struct nht_act_s nht_act_t;

/**
 * Hook that writes the real pin level (active_high already applied).
 * If NULL in the config, the ESP-IDF GPIO driver is used.
 *
 * @param pin   GPIO number from the config (may be negative for virtual outputs)
 * @param level physical level to drive
 * @param user  pointer from nht_act_cfg_t::user
 */
typedef void (*nht_act_write_t)(int pin, bool level, void *user);

/**
 * Event callback.
 *
 * @param a    the actuator that raised the event
 * @param ev   what happened
 * @param user pointer given to nht_act_on_event()
 */
typedef void (*nht_act_evt_cb_t)(nht_act_t *a, nht_act_event_t ev, void *user);

/** Static configuration of one output. */
typedef struct {
    int      pin;          /**< GPIO number; < 0 means a virtual output          */
    bool     active_high;  /**< false = active-low relay board                   */
    uint32_t auto_off_ms;  /**< default run time; 0 = stay on until told off     */
    uint32_t on_delay_ms;  /**< delay between the command and closing the circuit */
    uint32_t min_on_ms;    /**< must stay on at least this long before off       */
    uint32_t min_off_ms;   /**< must stay off at least this long before on       */
    nht_act_startup_t startup; /**< level after init                             */
    nht_act_write_t   write;   /**< pin-write hook; NULL = ESP-IDF GPIO          */
    void             *user;    /**< passed to @ref write                         */
    const char       *name;    /**< used in logs; may be NULL                    */
} nht_act_cfg_t;

/** Runtime state. Treat as read-only; use the API to change it. */
struct nht_act_s {
    nht_act_cfg_t   cfg;
    nht_act_state_t state;
    bool     inhibit;
    uint32_t once_ms;         /**< duration of the current one-shot run          */
    uint32_t state_since_ms;
    uint32_t on_at_ms;        /**< when the circuit really closed (deadline base) */
    uint32_t deadline_ms;
    bool     has_deadline;
    uint32_t cmd_at_ms;       /**< when the on command arrived (for on_delay)    */
    nht_act_evt_cb_t on_event;
    void            *evt_user;
};

/**
 * Prepare the struct and drive the pin to its startup level.
 *
 * @param a      actuator to initialise
 * @param cfg    configuration (copied)
 * @param now_ms current time in milliseconds
 * @return NHT_ACT_OK, or NHT_ACT_ERR_PARAM for NULL arguments
 */
nht_act_err_t nht_act_init(nht_act_t *a, const nht_act_cfg_t *cfg,
                           uint32_t now_ms);

/**
 * Register the event callback (one per actuator).
 *
 * @param a    actuator
 * @param cb   callback, or NULL to remove
 * @param user opaque pointer handed back to the callback
 */
void nht_act_on_event(nht_act_t *a, nht_act_evt_cb_t cb, void *user);

/**
 * Switch on with the default duration (cfg.auto_off_ms).
 * If already on, the existing deadline - even one set by nht_act_on_for() - is
 * left untouched.
 *
 * @return NHT_ACT_OK, NHT_ACT_ERR_INHIBIT or NHT_ACT_ERR_MIN_OFF
 */
nht_act_err_t nht_act_on(nht_act_t *a, uint32_t now_ms);

/**
 * Switch on for exactly @p ms milliseconds, counted from the moment the circuit
 * really closes. `ms == 0` means "use cfg.auto_off_ms". If already on, only the
 * deadline is reset, so repeating the call with the same duration is harmless.
 *
 * @param a      actuator
 * @param ms     run time in milliseconds
 * @param now_ms current time
 * @return NHT_ACT_OK, NHT_ACT_ERR_INHIBIT or NHT_ACT_ERR_MIN_OFF
 */
nht_act_err_t nht_act_on_for(nht_act_t *a, uint32_t ms, uint32_t now_ms);

/**
 * Switch on if off; if already on, recompute the auto-off deadline from *now*.
 * Does not switch the pin again, so there is no glitch. Use it for "stay on a
 * while after the last activity" outputs.
 *
 * @return NHT_ACT_OK, or the error of nht_act_on() when it was off
 */
nht_act_err_t nht_act_retrigger(nht_act_t *a, uint32_t now_ms);

/**
 * Add @p ms to the current deadline, counted from now.
 *
 * @return NHT_ACT_OK, or the error of nht_act_on_for() when it was off
 */
nht_act_err_t nht_act_extend(nht_act_t *a, uint32_t ms, uint32_t now_ms);

/**
 * Switch off, honouring min_on_ms.
 *
 * @return NHT_ACT_OK or NHT_ACT_ERR_MIN_ON
 */
nht_act_err_t nht_act_off(nht_act_t *a, uint32_t now_ms);

/**
 * Switch off regardless of min_on_ms. For emergency stops and safety inhibits.
 *
 * @return NHT_ACT_OK, or NHT_ACT_ERR_PARAM for a NULL actuator
 */
nht_act_err_t nht_act_off_force(nht_act_t *a, uint32_t now_ms);

/** Convenience: nht_act_on() or nht_act_off() depending on @p on. */
nht_act_err_t nht_act_set(nht_act_t *a, bool on, uint32_t now_ms);

/**
 * Synchronise with a state reported from elsewhere (cloud, another device),
 * with a guard: if the requested state already matches, do nothing.
 *
 * Use this - not nht_act_set() - when applying remote state. Calling set()
 * without comparing is what caused the "wrong run time" bug in the previous
 * implementation.
 */
nht_act_err_t nht_act_sync(nht_act_t *a, bool on, uint32_t now_ms);

/**
 * Set or clear the safety inhibit. Setting it forces the output off at once and
 * rejects every turn-on command until it is cleared.
 */
void nht_act_set_inhibit(nht_act_t *a, bool inhibit, uint32_t now_ms);

/**
 * Advance the state machine: ends the on-delay and fires the auto-off.
 * Call it whenever nht_act_next_wake_ms() elapses and after every command.
 */
void nht_act_tick(nht_act_t *a, uint32_t now_ms);

/**
 * How long until nht_act_tick() has something to do (end of on-delay or
 * auto-off deadline). Lets a caller sleep instead of polling.
 *
 * @param[out] out_ms milliseconds from @p now_ms (0 = due now)
 * @return false if nothing is pending and any sleep length is fine
 */
bool nht_act_next_wake_ms(const nht_act_t *a, uint32_t now_ms, uint32_t *out_ms);

/**
 * Milliseconds left before the auto-off.
 *
 * @return 0 if there is no deadline or the output is off
 */
uint32_t nht_act_remaining_ms(const nht_act_t *a, uint32_t now_ms);

/** @return true when the circuit is really closed (not merely pending). */
static inline bool nht_act_is_on(const nht_act_t *a)
{
    return a->state == NHT_ACT_ON;
}

/** @return true while the safety inhibit is set. */
static inline bool nht_act_is_inhibited(const nht_act_t *a)
{
    return a->inhibit;
}

/** Human-readable state name ("off", "pending", "on"). */
const char *nht_act_state_str(nht_act_state_t s);

#ifdef __cplusplus
}
#endif
#endif /* NHT_ACTUATOR_H */
