/**
 * @file    nht_store.c
 * @brief   Key-value storage on NVS.
 *
 * See nht_store.h for the public API and the design rationale.
 */

#include "nht_store.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "nht_store";

esp_err_t nht_store_init(nht_store_t *st, const char *partition, const char *ns)
{
    if (!st || !ns) return ESP_ERR_INVALID_ARG;
    memset(st, 0, sizeof(*st));

    const char *part = partition ? partition : NVS_DEFAULT_PART_NAME;
    strncpy(st->part, part, sizeof(st->part) - 1);
    strncpy(st->ns, ns, sizeof(st->ns) - 1);

    esp_err_t err = nvs_flash_init_partition(part);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "partition '%s' hỏng hoặc đổi phiên bản, xoá và tạo lại",
                 part);
        ESP_ERROR_CHECK(nvs_flash_erase_partition(part));
        err = nvs_flash_init_partition(part);
    }
    if (err != ESP_OK && err != ESP_ERR_NVS_PART_NOT_FOUND) return err;

    err = nvs_open_from_partition(part, ns, NVS_READWRITE, &st->handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "không mở được namespace '%s': %s", ns,
                 esp_err_to_name(err));
        return err;
    }
    st->opened = true;
    return ESP_OK;
}

void nht_store_deinit(nht_store_t *st)
{
    if (st && st->opened) {
        nvs_close(st->handle);
        st->opened = false;
    }
}

void nht_store_set_deferred(nht_store_t *st, bool deferred)
{
    st->deferred = deferred;
}

esp_err_t nht_store_commit(nht_store_t *st)
{
    if (!st->opened) return ESP_ERR_INVALID_STATE;
    return nvs_commit(st->handle);
}

/** Commit right after writing, unless writes are being batched. */
static esp_err_t maybe_commit(nht_store_t *st, esp_err_t err)
{
    if (err != ESP_OK || st->deferred) return err;
    return nvs_commit(st->handle);
}

bool nht_store_has(nht_store_t *st, const char *key)
{
    if (!st->opened) return false;
    size_t len = 0;
    /* Try a few types: NVS has no generic "does this key exist" call. */
    if (nvs_get_str(st->handle, key, NULL, &len) == ESP_OK)  return true;
    if (nvs_get_blob(st->handle, key, NULL, &len) == ESP_OK) return true;
    int32_t i32;
    if (nvs_get_i32(st->handle, key, &i32) == ESP_OK) return true;
    uint32_t u32;
    if (nvs_get_u32(st->handle, key, &u32) == ESP_OK) return true;
    uint8_t u8;
    if (nvs_get_u8(st->handle, key, &u8) == ESP_OK) return true;
    return false;
}

esp_err_t nht_store_remove(nht_store_t *st, const char *key)
{
    if (!st->opened) return ESP_ERR_INVALID_STATE;
    esp_err_t err = nvs_erase_key(st->handle, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    return maybe_commit(st, err);
}

esp_err_t nht_store_erase_all(nht_store_t *st)
{
    if (!st->opened) return ESP_ERR_INVALID_STATE;
    return maybe_commit(st, nvs_erase_all(st->handle));
}

int32_t nht_store_get_i32(nht_store_t *st, const char *key, int32_t def)
{
    int32_t v;
    if (!st->opened || nvs_get_i32(st->handle, key, &v) != ESP_OK) return def;
    return v;
}

uint32_t nht_store_get_u32(nht_store_t *st, const char *key, uint32_t def)
{
    uint32_t v;
    if (!st->opened || nvs_get_u32(st->handle, key, &v) != ESP_OK) return def;
    return v;
}

bool nht_store_get_bool(nht_store_t *st, const char *key, bool def)
{
    uint8_t v;
    if (!st->opened || nvs_get_u8(st->handle, key, &v) != ESP_OK) return def;
    return v != 0;
}

float nht_store_get_float(nht_store_t *st, const char *key, float def)
{
    /* NVS has no float type: store the raw bits in a u32. */
    uint32_t bits;
    if (!st->opened || nvs_get_u32(st->handle, key, &bits) != ESP_OK) return def;
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

size_t nht_store_get_str(nht_store_t *st, const char *key,
                         char *out, size_t out_size, const char *def)
{
    if (!out || out_size == 0) return 0;

    size_t len = out_size;
    if (st->opened && nvs_get_str(st->handle, key, out, &len) == ESP_OK)
        return len ? len - 1 : 0;

    /* Calling `get_str(key, buf, sizeof buf, buf)` (the default is the
       destination buffer itself) is valid: do not clear `out` before reading
       `def`, or every default would vanish when the key is not in NVS. */
    if (def == out) {
        out[out_size - 1] = '\0';
        return strlen(out);
    }
    if (def) {
        strncpy(out, def, out_size - 1);
        out[out_size - 1] = '\0';
        return strlen(out);
    }
    out[0] = '\0';
    return 0;
}

esp_err_t nht_store_get_blob(nht_store_t *st, const char *key,
                             void *out, size_t *len)
{
    if (!st->opened) return ESP_ERR_INVALID_STATE;
    return nvs_get_blob(st->handle, key, out, len);
}

esp_err_t nht_store_set_i32(nht_store_t *st, const char *key, int32_t v)
{
    if (!st->opened) return ESP_ERR_INVALID_STATE;
    return maybe_commit(st, nvs_set_i32(st->handle, key, v));
}

esp_err_t nht_store_set_u32(nht_store_t *st, const char *key, uint32_t v)
{
    if (!st->opened) return ESP_ERR_INVALID_STATE;
    return maybe_commit(st, nvs_set_u32(st->handle, key, v));
}

esp_err_t nht_store_set_bool(nht_store_t *st, const char *key, bool v)
{
    if (!st->opened) return ESP_ERR_INVALID_STATE;
    return maybe_commit(st, nvs_set_u8(st->handle, key, v ? 1 : 0));
}

esp_err_t nht_store_set_float(nht_store_t *st, const char *key, float v)
{
    if (!st->opened) return ESP_ERR_INVALID_STATE;
    uint32_t bits;
    memcpy(&bits, &v, sizeof(bits));
    return maybe_commit(st, nvs_set_u32(st->handle, key, bits));
}

esp_err_t nht_store_set_str(nht_store_t *st, const char *key, const char *v)
{
    if (!st->opened) return ESP_ERR_INVALID_STATE;
    return maybe_commit(st, nvs_set_str(st->handle, key, v ? v : ""));
}

esp_err_t nht_store_set_blob(nht_store_t *st, const char *key,
                             const void *v, size_t len)
{
    if (!st->opened) return ESP_ERR_INVALID_STATE;
    return maybe_commit(st, nvs_set_blob(st->handle, key, v, len));
}
