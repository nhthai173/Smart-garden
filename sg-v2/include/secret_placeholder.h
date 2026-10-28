/**
 * @file    secret_placeholder.h
 * @brief   Development defaults. Copy to secret.h and fill in (secret.h is git-ignored).
 *
 * app_cfg.c detects this file with __has_include. If present, the keys below
 * become the defaults when NVS is blank; an end user can still re-enter them
 * through the captive portal, and NVS values always win.
 */
#ifndef SG_SECRET_H
#define SG_SECRET_H

/* Real WiFi, used by the build flashed to a board. */
#define SECRET_WIFI_SSID      ""
#define SECRET_WIFI_PASS      ""

/* The simulator provides an open network called Wokwi-GUEST that reaches the
   internet. Override only for the simulator build - do NOT make a build flashed to
   a real board go looking for a network that does not exist. */
#if defined(SG_WOKWI)
#undef  SECRET_WIFI_SSID
#undef  SECRET_WIFI_PASS
#define SECRET_WIFI_SSID      "Wokwi-GUEST"
#define SECRET_WIFI_PASS      ""
#endif

#define SECRET_FB_API_KEY     ""
#define SECRET_FB_DB_URL      ""
#define SECRET_FB_EMAIL       ""
#define SECRET_FB_PASS        ""

/* A dedicated branch for experiments - do NOT touch the data of the v1 devices
   running in service. */
#define SECRET_FB_DEVICE_ID   "sg-v2-dev"

#endif /* SG_SECRET_H */
