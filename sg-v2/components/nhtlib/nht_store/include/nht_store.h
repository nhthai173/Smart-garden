/**
 * @file    nht_store.h
 * @brief   Key-value settings storage on NVS. The ESP-IDF counterpart of an
 *          ENVFile.
 * @version 1.0.1
 * @license MIT
 *
 * Why NVS instead of a file on LittleFS/SPIFFS: NVS already has wear levelling
 * and transactional writes, and survives a power cut in the middle of a write.
 * A device that loses power at any moment should not bet its configuration on
 * a filesystem whose structure can be corrupted.
 *
 * Every getter takes a default and never reports an error: a missing key or a
 * wrong type simply returns the default. The firmware therefore works from a
 * blank NVS with no "initialise settings" step.
 *
 * @par Example
 * @code
 * nht_store_t cfg;
 * nht_store_init(&cfg, "nvs_cfg", "app");
 * char ssid[33] = "default-ssid";
 * nht_store_get_str(&cfg, "ssid", ssid, sizeof ssid, ssid);  // default == buffer is fine
 * uint32_t run_s = nht_store_get_u32(&cfg, "run_s", 60);
 * nht_store_set_u32(&cfg, "run_s", 120);
 * @endcode
 */

#ifndef NHT_STORE_H
#define NHT_STORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "nvs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Store handle. */
typedef struct {
    nvs_handle_t handle;
    bool         opened;
    bool         deferred;   /**< true = batch writes, commit manually */
    char         ns[16];
    char         part[16];
} nht_store_t;

/**
 * Open a namespace.
 *
 * @param st        store to initialise
 * @param partition NVS partition name; NULL = the default "nvs"
 * @param ns        namespace, at most 15 characters
 * @return ESP_OK or an NVS error
 */
esp_err_t nht_store_init(nht_store_t *st, const char *partition, const char *ns);

/** Close the namespace. */
void      nht_store_deinit(nht_store_t *st);

/**
 * Choose when writes reach flash.
 *
 * @param deferred true = the set_* calls only buffer until nht_store_commit()
 */
void      nht_store_set_deferred(nht_store_t *st, bool deferred);

/** Flush buffered writes to flash. */
esp_err_t nht_store_commit(nht_store_t *st);

/** @return true if @p key exists. */
bool      nht_store_has(nht_store_t *st, const char *key);

/** Delete one key. */
esp_err_t nht_store_remove(nht_store_t *st, const char *key);

/** Delete every key in the namespace. */
esp_err_t nht_store_erase_all(nht_store_t *st);

/** @name Getters - return @p def when the key is missing or of another type
 *  @{ */
int32_t   nht_store_get_i32  (nht_store_t *st, const char *key, int32_t  def);
uint32_t  nht_store_get_u32  (nht_store_t *st, const char *key, uint32_t def);
bool      nht_store_get_bool (nht_store_t *st, const char *key, bool     def);
float     nht_store_get_float(nht_store_t *st, const char *key, float    def);
/** @} */

/**
 * Read a string.
 *
 * @p def may point to the same buffer as @p out (`get_str(k, buf, n, buf)`
 * keeps the buffer's current content as the default).
 *
 * @param out      destination buffer
 * @param out_size size of @p out
 * @param def      value to use when the key is missing; may be NULL
 * @return number of characters copied, not counting the terminator
 */
size_t    nht_store_get_str(nht_store_t *st, const char *key,
                            char *out, size_t out_size, const char *def);

/**
 * Read a binary blob.
 *
 * @param[in,out] len in: buffer size; out: bytes actually read
 * @return ESP_OK or an NVS error
 */
esp_err_t nht_store_get_blob(nht_store_t *st, const char *key,
                             void *out, size_t *len);

/** @name Setters
 *  @{ */
esp_err_t nht_store_set_i32  (nht_store_t *st, const char *key, int32_t  v);
esp_err_t nht_store_set_u32  (nht_store_t *st, const char *key, uint32_t v);
esp_err_t nht_store_set_bool (nht_store_t *st, const char *key, bool     v);
esp_err_t nht_store_set_float(nht_store_t *st, const char *key, float    v);
esp_err_t nht_store_set_str  (nht_store_t *st, const char *key, const char *v);
esp_err_t nht_store_set_blob (nht_store_t *st, const char *key,
                              const void *v, size_t len);
/** @} */

#ifdef __cplusplus
}
#endif
#endif /* NHT_STORE_H */
