/**
 * @file    nht_sched.h
 * @brief   Time-of-day scheduler with a catch-up window. Pure logic with no
 *          ESP-IDF dependency - it compiles and unit-tests on a PC.
 * @version 1.0.0
 * @license MIT
 *
 * The library knows nothing about what it is scheduling: each task carries a
 * raw `payload` defined by the application. For watering the payload is
 * {duration, valve opening}; another project can put anything that fits.
 *
 * @par Sizing the payload
 * Set the sizes with build flags (platformio.ini) or Kconfig:
 * @code
 * build_flags = -D NHT_SCHED_PAYLOAD_SIZE=8
 * build_flags = -D NHT_SCHED_MAX_TASKS=16
 * @endcode
 *
 * @par Why a catch-up window
 * The naive scheduler matches the exact current minute
 * (`hour == h && minute == m`). It breaks as soon as the main loop is delayed
 * by more than a minute - an OTA, waiting for the network, a TLS handshake -
 * and that day's run is then lost, silently, with no trace.
 *
 * Here a due task may still run for `catchup_window_s` seconds after its
 * scheduled time, and `last_run_day` guarantees it runs only once per day.
 *
 * @par Example
 * @code
 * nht_sched_t sched;
 * nht_sched_init(&sched, 0);                       // default 600 s window
 *
 * nht_sched_task_t t = { .hour = 6, .minute = 30,
 *                        .repeat_mask = NHT_SCHED_REPEAT_DAILY, .enabled = true };
 * uint16_t secs = 900;
 * nht_sched_set_payload(&t, &secs, sizeof secs);
 * uint8_t id;
 * nht_sched_add(&sched, &t, &id);
 *
 * // once a second, when the wall clock is valid:
 * nht_time_t now;
 * nht_time_from_unix(time(NULL), 7 * 3600, &now);
 * nht_sched_task_t due;
 * while (nht_sched_due(&sched, &now, &due)) { start_watering(due.payload); }
 * @endcode
 */

#ifndef NHT_SCHED_H
#define NHT_SCHED_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of tasks held at once. */
#ifndef NHT_SCHED_MAX_TASKS
#define NHT_SCHED_MAX_TASKS 16
#endif

/** Size in bytes of each task's application payload. */
#ifndef NHT_SCHED_PAYLOAD_SIZE
#define NHT_SCHED_PAYLOAD_SIZE 8
#endif

/** Default catch-up window: 10 minutes. */
#define NHT_SCHED_DEFAULT_CATCHUP_S 600u

/** @name Repeat masks - bit0 = Sunday ... bit6 = Saturday
 *  @{ */
#define NHT_SCHED_REPEAT_NONE   0x00u   /**< run once, then disable itself */
#define NHT_SCHED_REPEAT_DAILY  0x7Fu   /**< every day                     */
#define NHT_SCHED_REPEAT_WEEKDAY 0x3Eu  /**< Monday to Friday              */
#define NHT_SCHED_REPEAT_WEEKEND 0x41u  /**< Sunday and Saturday           */
/** @} */

/** Compile-time check that an application payload struct fits. */
#define NHT_SCHED_ASSERT_PAYLOAD(type)                                    \
    typedef char nht_sched_payload_too_big_##type[                        \
        (sizeof(type) <= NHT_SCHED_PAYLOAD_SIZE) ? 1 : -1]

/** One scheduled task. */
typedef struct {
    uint8_t  id;            /**< 1..255; 0 = empty slot                       */
    uint8_t  hour;          /**< 0..23                                        */
    uint8_t  minute;        /**< 0..59                                        */
    uint8_t  repeat_mask;   /**< NHT_SCHED_REPEAT_*                           */
    bool     enabled;
    uint32_t last_run_day;  /**< day number (since epoch) of the last run     */
    uint8_t  payload[NHT_SCHED_PAYLOAD_SIZE]; /**< application data           */
} nht_sched_task_t;

/** The schedule: a fixed array of tasks. Persist it as a blob if you wish. */
typedef struct {
    nht_sched_task_t tasks[NHT_SCHED_MAX_TASKS];
    uint8_t          count;
    uint8_t          next_id;
    uint16_t         catchup_window_s;
} nht_sched_t;

/**
 * Local wall-clock time, already split up.
 *
 * The scheduler takes this instead of `time_t` so that PC tests do not touch the
 * operating system's time zone.
 */
typedef struct {
    uint32_t day;    /**< days since epoch in LOCAL time; 0 = clock not valid yet */
    uint8_t  wday;   /**< 0 = Sunday ... 6 = Saturday */
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
} nht_time_t;

/**
 * Convert a Unix timestamp plus a time-zone offset to @ref nht_time_t.
 *
 * @param unix_s       seconds since 1970-01-01 UTC
 * @param tz_offset_s  offset from UTC in seconds (for example 7 * 3600)
 * @param[out] out     result
 */
void nht_time_from_unix(int64_t unix_s, int32_t tz_offset_s, nht_time_t *out);

/**
 * Empty the schedule and set the catch-up window.
 *
 * @param s                schedule
 * @param catchup_window_s window in seconds; 0 = default (600 s)
 */
void nht_sched_init(nht_sched_t *s, uint16_t catchup_window_s);

/**
 * Add a task. The `id` field of @p t is ignored; the new id is returned through
 * @p out_id.
 *
 * @return false if the schedule is full
 */
bool nht_sched_add(nht_sched_t *s, const nht_sched_task_t *t, uint8_t *out_id);

/**
 * Update the task whose id equals `t->id`, in place.
 * `last_run_day` is kept unless the scheduled time changed, in which case it is
 * reset so the task may run again today.
 *
 * @return false if no such task exists
 */
bool nht_sched_update(nht_sched_t *s, const nht_sched_task_t *t);

/** Remove a task by id. @return false if it does not exist. */
bool nht_sched_remove(nht_sched_t *s, uint8_t id);

/** Enable or disable a task by id. @return false if it does not exist. */
bool nht_sched_set_enabled(nht_sched_t *s, uint8_t id, bool enabled);

/** Remove every task. */
void nht_sched_clear(nht_sched_t *s);

/** @return pointer to the task with this id, or NULL. */
nht_sched_task_t *nht_sched_find(nht_sched_t *s, uint8_t id);

/**
 * Take one task that is due at @p now and mark it as run for today.
 *
 * Call repeatedly until it returns 0 to collect every task that is due at the
 * same moment.
 *
 * @param s    schedule
 * @param now  local time (day == 0 means "clock not valid": nothing is due)
 * @param[out] out copy of the due task (may be NULL)
 * @return the task id (> 0), or 0 if nothing is due
 */
uint8_t nht_sched_due(nht_sched_t *s, const nht_time_t *now,
                      nht_sched_task_t *out);

/**
 * Seconds until the next run of any task.
 *
 * @return seconds, or UINT32_MAX if there is none
 */
uint32_t nht_sched_next_in(const nht_sched_t *s, const nht_time_t *now);

/** @return pointer to the task's payload bytes. */
static inline void *nht_sched_payload(nht_sched_task_t *t) { return t->payload; }

/**
 * Copy application data into a task's payload (zero-padded, truncated to
 * NHT_SCHED_PAYLOAD_SIZE).
 *
 * @param t   task
 * @param src data to copy; NULL clears the payload
 * @param len number of bytes in @p src
 */
static inline void nht_sched_set_payload(nht_sched_task_t *t,
                                         const void *src, uint32_t len)
{
    memset(t->payload, 0, NHT_SCHED_PAYLOAD_SIZE);
    if (src && len) {
        memcpy(t->payload, src,
               len > NHT_SCHED_PAYLOAD_SIZE ? NHT_SCHED_PAYLOAD_SIZE : len);
    }
}

#ifdef __cplusplus
}
#endif
#endif /* NHT_SCHED_H */
