/**
 * @file    app_cfg.h
 * @brief   Every adjustable knob of the device, stored in NVS.
 *
 * Rule: NO physical constant is hard-coded in the logic. Ball valves turn at
 * different speeds, leak probes differ between dry and damp, and every board's
 * voltage divider is off in its own way - all of it must be adjustable in the
 * field through the web UI, without reflashing the firmware.
 *
 * The defaults below are only a sensible starting point so the device runs
 * straight from a blank NVS.
 */
#ifndef APP_CFG_H
#define APP_CFG_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define CFG_STR_LEN   64
#define CFG_URL_LEN  128

typedef struct {
    /* --- van --- */
    uint8_t  valve_type;      /**< 0 = normally-closed solenoid, 1 = motorised ball valve */
    uint32_t travel_ms;       /**< ball valve: time to travel end to end       */
    uint32_t pump_delay_ms;
    uint32_t ac_auto_off_ms;
    uint32_t run_default_s;
    uint32_t run_max_s;       /**< HARD CEILING for one watering run           */

    /* --- leak sensor --- */
    bool     leak_analog;
    bool     leak_inverted;
    uint16_t leak_thr_on;
    uint16_t leak_thr_off;
    uint32_t leak_hold_on_ms;
    uint32_t leak_hold_off_ms;

    /* --- flow and voltage --- */
    float    flow_pulses_per_l;
    float    volt_scale;      /**< voltage-divider ratio                       */
    float    volt_min;        /**< below this the supply counts as faulty      */

    /* --- schedule and time --- */
    int32_t  tz_offset_s;
    uint16_t catchup_window_s;

    /* --- identity and network --- */
    char     dev_name[CFG_STR_LEN];
    char     wifi_ssid[CFG_STR_LEN];
    char     wifi_pass[CFG_STR_LEN];

    /* --- firebase --- */
    char     fb_api_key[CFG_STR_LEN];
    char     fb_db_url[CFG_URL_LEN];
    char     fb_email[CFG_STR_LEN];
    char     fb_pass[CFG_STR_LEN];
    char     fb_device_id[CFG_STR_LEN];
} app_cfg_t;

/** Load settings from NVS, filling defaults for missing keys. */
esp_err_t app_cfg_load(void);

/** @return pointer to the live settings. Never NULL after app_cfg_load(). */
app_cfg_t *app_cfg(void);

/** Write all current settings to NVS. */
esp_err_t app_cfg_save(void);

/** Set one setting by name (used by the REST /config). @return false for an unknown name. */
bool app_cfg_set_kv(const char *key, const char *value);

/** Export the settings as JSON. Passwords are masked. */
int  app_cfg_to_json(char *buf, size_t len);

/** Erase all settings and return to the factory defaults. */
esp_err_t app_cfg_factory_reset(void);

bool app_cfg_wifi_ready(void);
bool app_cfg_firebase_ready(void);

#endif /* APP_CFG_H */
