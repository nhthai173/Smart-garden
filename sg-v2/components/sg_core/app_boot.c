/**
 * @file    app_boot.c
 * @brief   Records why the chip reset.
 *
 * See app_boot.h.
 */

#include "app_boot.h"

static esp_reset_reason_t g_reason;

void app_boot_capture(void) { g_reason = esp_reset_reason(); }

esp_reset_reason_t app_last_reset_reason(void) { return g_reason; }

bool app_last_reset_abnormal(void)
{
    switch (g_reason) {
    case ESP_RST_PANIC: case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:   case ESP_RST_BROWNOUT:
        return true;
    default:
        return false;
    }
}

const char *app_last_reset_str(void)
{
    switch (g_reason) {
    case ESP_RST_POWERON:   return "cắm nguồn";
    case ESP_RST_SW:        return "phần mềm gọi restart";
    case ESP_RST_PANIC:     return "PANIC (crash)";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog";
    case ESP_RST_WDT:       return "watchdog khác";
    case ESP_RST_BROWNOUT:  return "sụt áp (brownout)";
    case ESP_RST_DEEPSLEEP: return "thức khỏi deep sleep";
    case ESP_RST_EXT:       return "reset ngoài";
    default:                return "không rõ";
    }
}
