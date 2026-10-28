/**
 * @file    app_water.h
 * @brief   Watering logic - the device's source of truth.
 *
 * This layer knows NOTHING about WiFi, HTTP or Firebase. The web server and the
 * Firebase adapter both call into it; they are add-ons, not the core. Pull the
 * network out and the two adapters go quiet while the schedule keeps running.
 *
 * That is exactly where the old system had it backwards: the schedule list was
 * loaded only inside the "first Firebase connection" callback, so a reboot while
 * offline meant no watering at all.
 */
#ifndef APP_WATER_H
#define APP_WATER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "nht_valve.h"
#include "nht_sched.h"

/** Who issued a command - used for logs, and so an adapter can tell its own commands apart. */
typedef enum {
    WSRC_BOOT = 0,
    WSRC_LOCAL_API,
    WSRC_SCHEDULE,
    WSRC_CLOUD,
    WSRC_BUTTON,
    WSRC_SAFETY,
} water_src_t;

/**
 * Faults that hold the valve shut. Any bit set = the valve is CLOSED AT ONCE and
 * locked against opening; only when every bit is clear (after noise filtering) may it open again.
 */
#define WF_LEAK         0x01   /**< water detected by the leak sensor          */
#define WF_LEAK_SENSOR  0x02   /**< leak sensor broken: we have no "eyes" left  */
#define WF_VOLTAGE      0x04   /**< supply below threshold: the motor lacks torque */

/** Payload of one watering schedule - the only watering-specific data put into nht_sched. */
typedef struct {
    uint16_t duration_s;
    uint8_t  level_pct;   /**< 1..100, only meaningful for a ball valve */
} water_task_t;

typedef struct {
    nht_valve_state_t valve;
    uint32_t remaining_s;
    bool     leak;
    uint16_t leak_raw;
    float    voltage;
    bool     voltage_low;
    float    flow_lpm;
    float    flow_total_l;
    uint32_t uptime_s;
    water_src_t last_src;
    uint8_t  sched_count;
    bool     time_valid;
    uint8_t  faults;      /**< WF_* bit mask; 0 = healthy */
} water_status_t;

/** Called on any change worth reporting (valve state, leak...). */
typedef void (*water_change_cb_t)(const water_status_t *st, void *user);

esp_err_t water_init(void);
esp_err_t water_start(void);

/**
 * Push settings just changed in app_cfg into the RUNNING structures.
 *
 * If leak-sensor thresholds could only take effect after a reboot, nobody could
 * tune them in the garden - it takes a few rounds to find the right numbers.
 *
 * What can NOT change live (needs a reboot): the valve type, switching between
 * analog and digital sensing, and the GPIO pins - they decide which peripherals
 * are allocated at initialisation.
 */
void water_apply_cfg(void);

/** Register a listener. At most WATER_MAX_LISTENERS. */
bool water_on_change(water_change_cb_t cb, void *user);

/** Raise the change notification now (for an adapter that has just connected). */
void water_notify(void);

/**
 * @param seconds 0 = use the default duration. Always clamped to run_max_s.
 * @param level   0 = 100%.
 */
nht_valve_err_t water_open(uint32_t seconds, uint8_t level, water_src_t src);
nht_valve_err_t water_close(water_src_t src);

void water_get_status(water_status_t *out);
int  water_status_json(char *buf, size_t len);

/* --- schedule --- */
int  water_sched_json(char *buf, size_t len);
bool water_sched_add(uint8_t hour, uint8_t minute, uint8_t repeat_mask,
                     uint16_t duration_s, uint8_t level, bool enabled,
                     uint8_t *out_id);
bool water_sched_update(uint8_t id, uint8_t hour, uint8_t minute,
                        uint8_t repeat_mask, uint16_t duration_s,
                        uint8_t level, bool enabled);
bool water_sched_remove(uint8_t id);
void water_sched_clear(void);

/** Replace the whole schedule with a JSON array (used when syncing from the cloud). */
bool water_sched_replace_json(const char *json);

const char *water_src_str(water_src_t s);

/** "leak+voltage" ...; "" when there is no fault. buf must be >= 40 bytes. */
const char *water_faults_str(uint8_t faults, char *buf, size_t len);

#if defined(SG_BENCH)
/* --- Bench build only: simulate I/O and inject faults so the firmware can be tested on
   a real board with no sensors or relays wired. NOT part of the release firmware. --- */
void water_bench_set_leak_raw(uint16_t raw);
void water_bench_set_volt_raw(uint16_t raw);
void water_bench_fail_leak_adc(bool fail);
/** Stall the control task for `ms` milliseconds to prove the watchdog closes the valve. */
void water_bench_stall_ctl(uint32_t ms);
/** The REAL level currently driven on each relay pin (reads the output register). */
int  water_bench_pins_json(char *buf, size_t len);
#endif

#endif /* APP_WATER_H */
