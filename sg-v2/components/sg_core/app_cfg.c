/**
 * @file    app_cfg.c
 * @brief   Device settings stored in NVS, with development defaults.
 *
 * See app_cfg.h for the list of settings and the rules they follow.
 */

#include "app_cfg.h"
#include "nht_store.h"
#include "esp_log.h"
#include "esp_mac.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "cfg";

/*
 * Development defaults. Detected with __has_include so a clean checkout still
 * builds: without secret.h the device starts with blank settings and waits for
 * the user to enter them through the captive portal.
 *
 * NVS ALWAYS WINS - these are only the initial values when NVS holds nothing.
 */
#if __has_include("secret.h")
#include "secret.h"
#define HAVE_SECRET 1
#endif

/* Dedicated partition: re-formatting the settings never touches the WiFi
   calibration kept in the default "nvs" partition. */
#define CFG_PARTITION "nvs_cfg"
#define CFG_NAMESPACE "sg"

static app_cfg_t  C;
static nht_store_t ST;
static bool        ready;

app_cfg_t *app_cfg(void) { return &C; }

static void set_defaults(void)
{
    memset(&C, 0, sizeof(C));

    C.valve_type      = 1;          /* ball valve - same as the system in service */
    C.travel_ms       = 8000;
    C.pump_delay_ms   = 8000;
    C.ac_auto_off_ms  = 180000;
    C.run_default_s   = 60;
    C.run_max_s       = 1800;       /* 30 minutes */

    C.leak_analog     = true;
    C.leak_inverted   = false;
    C.leak_thr_on     = 2400;
    C.leak_thr_off    = 1200;
    C.leak_hold_on_ms  = 5000;      /* enter fast: safety first            */
    C.leak_hold_off_ms = 600000;    /* leave slowly: still damp = still flooded */

    C.flow_pulses_per_l = 450.0f;   /* typical YF-S201 */
    C.volt_scale        = 11.0f;
    C.volt_min          = 10.5f;

    C.tz_offset_s      = 7 * 3600;
    C.catchup_window_s = 600;

    snprintf(C.dev_name, sizeof(C.dev_name), "Smart Garden");

#if defined(HAVE_SECRET)
    snprintf(C.wifi_ssid,  sizeof(C.wifi_ssid),  "%s", SECRET_WIFI_SSID);
    snprintf(C.wifi_pass,  sizeof(C.wifi_pass),  "%s", SECRET_WIFI_PASS);
    snprintf(C.fb_api_key, sizeof(C.fb_api_key), "%s", SECRET_FB_API_KEY);
    snprintf(C.fb_db_url,  sizeof(C.fb_db_url),  "%s", SECRET_FB_DB_URL);
    snprintf(C.fb_email,   sizeof(C.fb_email),   "%s", SECRET_FB_EMAIL);
    snprintf(C.fb_pass,    sizeof(C.fb_pass),    "%s", SECRET_FB_PASS);
    snprintf(C.fb_device_id, sizeof(C.fb_device_id), "%s", SECRET_FB_DEVICE_ID);
    return;   /* device_id comes from the secrets, not derived from the MAC */
#endif

    /* The default device_id comes from the MAC so every board has its own
       branch on Firebase without manual configuration. */
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(C.fb_device_id, sizeof(C.fb_device_id), "sg-%02x%02x%02x",
             mac[3], mac[4], mac[5]);
}

#define LD_U32(field, key)  C.field = nht_store_get_u32 (&ST, key, C.field)
#define LD_I32(field, key)  C.field = nht_store_get_i32 (&ST, key, C.field)
#define LD_BOOL(field, key) C.field = nht_store_get_bool(&ST, key, C.field)
#define LD_F(field, key)    C.field = nht_store_get_float(&ST, key, C.field)
#define LD_S(field, key)    nht_store_get_str(&ST, key, C.field, \
                                              sizeof(C.field), C.field)

esp_err_t app_cfg_load(void)
{
    set_defaults();

    esp_err_t err = nht_store_init(&ST, CFG_PARTITION, CFG_NAMESPACE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "không mở được NVS (%s), chạy với mặc định",
                 esp_err_to_name(err));
        ready = false;
        return err;
    }
    ready = true;

    LD_U32(valve_type, "vtype");
    LD_U32(travel_ms, "travel_ms");
    LD_U32(pump_delay_ms, "pump_dly");
    LD_U32(ac_auto_off_ms, "ac_off");
    LD_U32(run_default_s, "run_def");
    LD_U32(run_max_s, "run_max");

    LD_BOOL(leak_analog, "lk_analog");
    LD_BOOL(leak_inverted, "lk_inv");
    LD_U32(leak_thr_on, "lk_on");
    LD_U32(leak_thr_off, "lk_off");
    LD_U32(leak_hold_on_ms, "lk_hold_on");
    LD_U32(leak_hold_off_ms, "lk_hold_off");

    LD_F(flow_pulses_per_l, "flow_ppl");
    LD_F(volt_scale, "volt_scale");
    LD_F(volt_min, "volt_min");

    LD_I32(tz_offset_s, "tz");
    LD_U32(catchup_window_s, "catchup");

    LD_S(dev_name, "dev_name");
    LD_S(wifi_ssid, "wifi_ssid");
    LD_S(wifi_pass, "wifi_pass");
    LD_S(fb_api_key, "fb_api");
    LD_S(fb_db_url, "fb_url");
    LD_S(fb_email, "fb_email");
    LD_S(fb_pass, "fb_pass");
    LD_S(fb_device_id, "fb_dev");

    /* Reject absurd settings as they load: a zero run-time ceiling would mean
       a valve that never closes - exactly the kind of bug that floods a garden. */
    if (C.run_max_s == 0 || C.run_max_s > 7200) C.run_max_s = 1800;
    if (C.run_default_s == 0 || C.run_default_s > C.run_max_s)
        C.run_default_s = C.run_max_s < 60 ? C.run_max_s : 60;
    if (C.valve_type > 1) C.valve_type = 1;
    if (C.leak_thr_off >= C.leak_thr_on) {
        C.leak_thr_on  = 2400;
        C.leak_thr_off = 1200;
    }
    if (C.catchup_window_s == 0 || C.catchup_window_s > 3600)
        C.catchup_window_s = 600;
    if (C.flow_pulses_per_l <= 0.0f) C.flow_pulses_per_l = 450.0f;
    if (C.volt_scale <= 0.0f)        C.volt_scale = 11.0f;

    ESP_LOGI(TAG, "cấu hình: van=%s travel=%ums tưới mặc định=%us trần=%us",
             C.valve_type ? "bi" : "solenoid",
             (unsigned)C.travel_ms, (unsigned)C.run_default_s,
             (unsigned)C.run_max_s);
    return ESP_OK;
}

esp_err_t app_cfg_save(void)
{
    if (!ready) return ESP_ERR_INVALID_STATE;
    nht_store_set_deferred(&ST, true);

    nht_store_set_u32(&ST, "vtype", C.valve_type);
    nht_store_set_u32(&ST, "travel_ms", C.travel_ms);
    nht_store_set_u32(&ST, "pump_dly", C.pump_delay_ms);
    nht_store_set_u32(&ST, "ac_off", C.ac_auto_off_ms);
    nht_store_set_u32(&ST, "run_def", C.run_default_s);
    nht_store_set_u32(&ST, "run_max", C.run_max_s);

    nht_store_set_bool(&ST, "lk_analog", C.leak_analog);
    nht_store_set_bool(&ST, "lk_inv", C.leak_inverted);
    nht_store_set_u32(&ST, "lk_on", C.leak_thr_on);
    nht_store_set_u32(&ST, "lk_off", C.leak_thr_off);
    nht_store_set_u32(&ST, "lk_hold_on", C.leak_hold_on_ms);
    nht_store_set_u32(&ST, "lk_hold_off", C.leak_hold_off_ms);

    nht_store_set_float(&ST, "flow_ppl", C.flow_pulses_per_l);
    nht_store_set_float(&ST, "volt_scale", C.volt_scale);
    nht_store_set_float(&ST, "volt_min", C.volt_min);

    nht_store_set_i32(&ST, "tz", C.tz_offset_s);
    nht_store_set_u32(&ST, "catchup", C.catchup_window_s);

    nht_store_set_str(&ST, "dev_name", C.dev_name);
    nht_store_set_str(&ST, "wifi_ssid", C.wifi_ssid);
    nht_store_set_str(&ST, "wifi_pass", C.wifi_pass);
    nht_store_set_str(&ST, "fb_api", C.fb_api_key);
    nht_store_set_str(&ST, "fb_url", C.fb_db_url);
    nht_store_set_str(&ST, "fb_email", C.fb_email);
    nht_store_set_str(&ST, "fb_pass", C.fb_pass);
    nht_store_set_str(&ST, "fb_dev", C.fb_device_id);

    nht_store_set_deferred(&ST, false);
    return nht_store_commit(&ST);
}

esp_err_t app_cfg_factory_reset(void)
{
    if (!ready) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nht_store_erase_all(&ST);
    set_defaults();
    return err;
}

static bool set_str_field(char *dst, size_t cap, const char *v)
{
    strncpy(dst, v ? v : "", cap - 1);
    dst[cap - 1] = '\0';
    return true;
}

bool app_cfg_set_kv(const char *key, const char *value)
{
    if (!key || !value) return false;

#define KV_U32(name, field)  if (!strcmp(key, name)) \
        { C.field = (uint32_t)strtoul(value, NULL, 10); return true; }
#define KV_I32(name, field)  if (!strcmp(key, name)) \
        { C.field = (int32_t)strtol(value, NULL, 10); return true; }
#define KV_BOOL(name, field) if (!strcmp(key, name)) \
        { C.field = (!strcmp(value, "1") || !strcmp(value, "true")); return true; }
#define KV_F(name, field)    if (!strcmp(key, name)) \
        { C.field = strtof(value, NULL); return true; }
#define KV_S(name, field)    if (!strcmp(key, name)) \
        { return set_str_field(C.field, sizeof(C.field), value); }

    KV_U32("valve_type", valve_type);
    KV_U32("travel_ms", travel_ms);
    KV_U32("pump_delay_ms", pump_delay_ms);
    KV_U32("ac_auto_off_ms", ac_auto_off_ms);
    KV_U32("run_default_s", run_default_s);
    KV_U32("run_max_s", run_max_s);

    KV_BOOL("leak_analog", leak_analog);
    KV_BOOL("leak_inverted", leak_inverted);
    KV_U32("leak_thr_on", leak_thr_on);
    KV_U32("leak_thr_off", leak_thr_off);
    KV_U32("leak_hold_on_ms", leak_hold_on_ms);
    KV_U32("leak_hold_off_ms", leak_hold_off_ms);

    KV_F("flow_pulses_per_l", flow_pulses_per_l);
    KV_F("volt_scale", volt_scale);
    KV_F("volt_min", volt_min);

    KV_I32("tz_offset_s", tz_offset_s);
    KV_U32("catchup_window_s", catchup_window_s);

    KV_S("dev_name", dev_name);
    KV_S("wifi_ssid", wifi_ssid);
    KV_S("wifi_pass", wifi_pass);
    KV_S("fb_api_key", fb_api_key);
    KV_S("fb_db_url", fb_db_url);
    KV_S("fb_email", fb_email);
    KV_S("fb_pass", fb_pass);
    KV_S("fb_device_id", fb_device_id);

#undef KV_U32
#undef KV_I32
#undef KV_BOOL
#undef KV_F
#undef KV_S
    return false;
}

int app_cfg_to_json(char *buf, size_t len)
{
    return snprintf(buf, len,
        "{\"dev_name\":\"%s\","
        "\"valve_type\":%u,\"travel_ms\":%u,\"pump_delay_ms\":%u,"
        "\"ac_auto_off_ms\":%u,\"run_default_s\":%u,\"run_max_s\":%u,"
        "\"leak_analog\":%s,\"leak_inverted\":%s,"
        "\"leak_thr_on\":%u,\"leak_thr_off\":%u,"
        "\"leak_hold_on_ms\":%u,\"leak_hold_off_ms\":%u,"
        "\"flow_pulses_per_l\":%.1f,\"volt_scale\":%.2f,\"volt_min\":%.1f,"
        "\"tz_offset_s\":%d,\"catchup_window_s\":%u,"
        "\"wifi_ssid\":\"%s\",\"wifi_pass_set\":%s,"
        "\"fb_db_url\":\"%s\",\"fb_email\":\"%s\",\"fb_pass_set\":%s,"
        "\"fb_api_key_set\":%s,\"fb_device_id\":\"%s\"}",
        C.dev_name,
        C.valve_type, (unsigned)C.travel_ms, (unsigned)C.pump_delay_ms,
        (unsigned)C.ac_auto_off_ms, (unsigned)C.run_default_s,
        (unsigned)C.run_max_s,
        C.leak_analog ? "true" : "false",
        C.leak_inverted ? "true" : "false",
        C.leak_thr_on, C.leak_thr_off,
        (unsigned)C.leak_hold_on_ms, (unsigned)C.leak_hold_off_ms,
        C.flow_pulses_per_l, C.volt_scale, C.volt_min,
        (int)C.tz_offset_s, C.catchup_window_s,
        C.wifi_ssid, C.wifi_pass[0] ? "true" : "false",
        C.fb_db_url, C.fb_email, C.fb_pass[0] ? "true" : "false",
        C.fb_api_key[0] ? "true" : "false", C.fb_device_id);
}

bool app_cfg_wifi_ready(void)     { return C.wifi_ssid[0] != '\0'; }
bool app_cfg_firebase_ready(void)
{
    return C.fb_api_key[0] && C.fb_db_url[0] && C.fb_email[0] && C.fb_pass[0];
}
