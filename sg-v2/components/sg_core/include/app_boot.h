/**
 * @file    app_boot.h
 * @brief   Remembers why the chip reset, so a crash can be traced afterwards.
 */
#ifndef APP_BOOT_H
#define APP_BOOT_H

#include <stdbool.h>
#include "esp_system.h"

/** Call first thing in app_main, before anything else: records why this boot happened. */
void app_boot_capture(void);

/** @return the raw ESP-IDF reset reason of this boot. */
esp_reset_reason_t app_last_reset_reason(void);

/** @return a human-readable description of the reset reason. */
const char *app_last_reset_str(void);

/** @return true if the last reset was a fault (panic, watchdog, brown-out), not intentional. */
bool app_last_reset_abnormal(void);

#endif
