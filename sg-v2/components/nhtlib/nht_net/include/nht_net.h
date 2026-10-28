/**
 * @file    nht_net.h
 * @brief   WiFi station with automatic reconnect, a SoftAP + captive-portal
 *          fallback, and SNTP time sync.
 * @version 1.1.0
 * @license MIT
 *
 * For an unattended device, losing WiFi is normal, not a fault. This library is
 * built around that:
 *
 *  - Reconnects back off gradually (1 s, 2 s, 4 s ... up to 60 s) using a timer.
 *    Nothing blocks the caller or the system event loop: nht_net_start()
 *    returns at once.
 *  - With no WiFi password yet, or when joining keeps failing, a SoftAP with a
 *    captive portal starts so the user can enter credentials again - no need to
 *    reflash the firmware.
 *  - When the station joins a network the AP is switched off; after a long
 *    outage it is switched on again.
 *  - On mesh networks (several access points with one SSID) it scans every
 *    channel and joins the strongest one, and reconnects to find a better one
 *    when the signal drops below about -75 dBm.
 *
 * Losing the network must never affect control logic. The library only
 * reports events; no function here waits for anyone.
 *
 * @par Example
 * @code
 * nht_net_cfg_t cfg = {
 *     .sta_ssid = "home", .sta_pass = "secret",
 *     .ap_ssid = "MyDevice-setup", .hostname = "mydevice",
 *     .tz_offset_s = 7 * 3600,
 * };
 * nht_net_start(&cfg);                    // returns immediately
 * nht_net_on_state(on_net_state, NULL);   // learn when it connects
 * @endcode
 */

#ifndef NHT_NET_H
#define NHT_NET_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Network state. */
typedef enum {
    NHT_NET_IDLE = 0,
    NHT_NET_CONNECTING,
    NHT_NET_CONNECTED,
    NHT_NET_AP_MODE,     /**< broadcasting a SoftAP for configuration */
} nht_net_state_t;

/**
 * State-change callback.
 *
 * @param st   the new state
 * @param user pointer given to nht_net_on_state()
 */
typedef void (*nht_net_cb_t)(nht_net_state_t st, void *user);

/** Network configuration. Strings are copied. */
typedef struct {
    const char *sta_ssid;         /**< network to join; empty = go to AP mode   */
    const char *sta_pass;

    const char *ap_ssid;          /**< NULL = never use the fallback AP         */
    const char *ap_pass;          /**< NULL/"" = open network                   */
    uint32_t    ap_fallback_ms;   /**< try the station this long before the AP;
                                       0 -> 45 s                                */

    const char *hostname;         /**< mDNS/DHCP name; NULL -> "nht-device"     */
    int32_t     tz_offset_s;      /**< local time offset from UTC, seconds      */
    const char *sntp_server;      /**< NULL -> "pool.ntp.org"                   */
} nht_net_cfg_t;

/**
 * Start networking. Returns at once; joining runs in the background.
 *
 * @param cfg configuration
 * @return ESP_OK or an ESP-IDF error
 */
esp_err_t nht_net_start(const nht_net_cfg_t *cfg);

/** Stop WiFi and the captive portal. */
void      nht_net_stop(void);

/** @return the current state. */
nht_net_state_t nht_net_state(void);

/** Register the state-change callback (one only). */
void            nht_net_on_state(nht_net_cb_t cb, void *user);

/**
 * Current IP address (the station's, or the AP's while in configuration mode).
 *
 * @param buf output buffer for a dotted string
 * @param len size of @p buf
 * @return true if an address is available
 */
bool    nht_net_ip(char *buf, size_t len);

/** @return RSSI of the joined network in dBm, or 0 if not connected. */
int8_t  nht_net_rssi(void);

/** @return true once the clock has been set by SNTP (or is otherwise valid). */
bool    nht_net_time_synced(void);

/**
 * Change the WiFi credentials and retry at once, without a reboot.
 *
 * @return ESP_OK or an ESP-IDF error
 */
esp_err_t nht_net_set_sta(const char *ssid, const char *pass);

/**
 * Scan for networks.
 *
 * @param buf output buffer
 * @param len size of @p buf
 * @return number of bytes of the JSON array
 *         `[{"ssid":..,"rssi":..,"secure":bool}]` written to @p buf
 */
int nht_net_scan_json(char *buf, size_t len);

/**
 * Start a DNS server that answers every query with the AP's address, so a phone
 * pops up the captive-portal page.
 */
esp_err_t nht_net_captive_dns_start(void);

/** Stop the captive DNS server. */
void      nht_net_captive_dns_stop(void);

#ifdef __cplusplus
}
#endif
#endif /* NHT_NET_H */
