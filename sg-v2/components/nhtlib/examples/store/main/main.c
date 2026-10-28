/**
 * @file main.c
 * @brief nht_store example: a boot counter and a string setting kept in NVS.
 */
#include "nvs_flash.h"
#include "esp_log.h"
#include "nht_store.h"

static const char *TAG = "example";

void app_main(void)
{
    nvs_flash_init();

    nht_store_t st;
    nht_store_init(&st, NULL /* default "nvs" partition */, "example");

    /* Every getter takes a default, so this works from a blank NVS. */
    uint32_t boots = nht_store_get_u32(&st, "boots", 0) + 1;
    nht_store_set_u32(&st, "boots", boots);

    char name[32] = "unnamed";                       /* the default is the buffer itself */
    nht_store_get_str(&st, "name", name, sizeof name, name);

    ESP_LOGI(TAG, "boot #%u, device name '%s'", (unsigned)boots, name);
    nht_store_set_str(&st, "name", "greenhouse-1");
    nht_store_deinit(&st);
}
