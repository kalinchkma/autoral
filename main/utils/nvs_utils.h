/**
 * @brief Generic NVS (Non-Volatile Storage) utility for ESP32.
 *
 * Provides a simple key-value API for storing arbitrary data that
 * survives power cycles.  NVS is backed by flash memory, so data
 * persists even when the device is unplugged.
 *
 * Also provides engine-specific helpers for rule and param persistence.
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "control/engine_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Limits ──────────────────────────────────────────────────────────── */

/** Max key length (including null terminator) for NVS keys. */
#define NVS_UTIL_MAX_KEY_LEN   16

/** Max entries per namespace (ESP-IDF NVS limit). */
#define NVS_UTIL_MAX_ENTRIES   255

/* ── Lifecycle ───────────────────────────────────────────────────────── */

/**
 * @brief Initialize NVS flash (call once at boot).
 *        Handles partition erase on corrupted/incompatible NVS.
 * @return ESP_OK on success.
 */
esp_err_t nvs_util_flash_init(void);

/**
 * @brief Open (or reopen) an NVS namespace for read/write.
 * @param ns    Namespace name (max 15 chars).
 * @param out_h Handle to store the opened handle.
 * @return ESP_OK on success.
 */
esp_err_t nvs_util_open(const char *ns, void **out_h);

/**
 * @brief Close an NVS handle (call when done).
 * @param h Handle returned by nvs_util_open.
 */
void nvs_util_close(void *h);

/* ── Data operations ─────────────────────────────────────────────────── */

/**
 * @brief Store a blob (arbitrary bytes) under a key.
 *        Creates or overwrites the entry.
 * @param h    NVS handle from nvs_util_open.
 * @param key  Key string (max 15 chars).
 * @param data Pointer to data.
 * @param len  Data length in bytes.
 * @return ESP_OK on success.
 */
esp_err_t nvs_util_set_blob(void *h, const char *key,
                             const void *data, size_t len);

/**
 * @brief Retrieve a blob by key.
 * @param h    NVS handle.
 * @param key  Key string.
 * @param out  Buffer to receive data.
 * @param len  IN: buffer size, OUT: actual data size.
 * @return ESP_OK if found, ESP_ERR_NVS_NOT_FOUND otherwise.
 */
esp_err_t nvs_util_get_blob(void *h, const char *key,
                             void *out, size_t *len);

/**
 * @brief Store a uint8_t value.
 */
esp_err_t nvs_util_set_u8(void *h, const char *key, uint8_t val);

/**
 * @brief Retrieve a uint8_t value.
 */
esp_err_t nvs_util_get_u8(void *h, const char *key, uint8_t *out);

/**
 * @brief Store a uint32_t value.
 */
esp_err_t nvs_util_set_u32(void *h, const char *key, uint32_t val);

/**
 * @brief Retrieve a uint32_t value.
 */
esp_err_t nvs_util_get_u32(void *h, const char *key, uint32_t *out);

/**
 * @brief Store a float value (as blob).
 */
esp_err_t nvs_util_set_float(void *h, const char *key, float val);

/**
 * @brief Retrieve a float value (as blob).
 */
esp_err_t nvs_util_get_float(void *h, const char *key, float *out);

/**
 * @brief Delete a key and its value from NVS.
 * @return ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if key doesn't exist.
 */
esp_err_t nvs_util_delete(void *h, const char *key);

/**
 * @brief Check if a key exists in NVS.
 * @return true if the key exists, false otherwise.
 */
bool nvs_util_exists(void *h, const char *key);

/**
 * @brief Erase all entries in the namespace.
 * @return ESP_OK on success.
 */
esp_err_t nvs_util_erase_all(void *h);

/**
 * @brief Get the number of used entries in a namespace.
 *        Note: this iterates and may be slow; cache the result.
 * @return Approximate count of entries.
 */
uint16_t nvs_util_count(void *h);

/* ── Engine rule / param persistence ─────────────────────────────────── */

/**
 * @brief Save an engine rule to NVS (with read-back verification).
 * @param h     NVS handle.
 * @param rule  Pointer to the rule to persist.
 */
esp_err_t nvs_util_save_rule(void *h, const engine_rule_t *rule);

/**
 * @brief Erase a rule from NVS by ID.
 */
esp_err_t nvs_util_erase_rule(void *h, uint8_t rule_id);

/**
 * @brief Load all persisted rules from NVS into an array.
 *        Enumerates all "rule_*" keys automatically.
 * @param h          NVS handle.
 * @param out_rules  Destination array (caller-allocated).
 * @param max_count  Max entries the array can hold.
 * @param out_count  Number of rules actually loaded.
 */
void nvs_util_load_rules(void *h, engine_rule_t *out_rules,
                         uint8_t max_count, uint8_t *out_count);

/**
 * @brief Save a param float value to NVS.
 */
esp_err_t nvs_util_save_param_value(void *h, uint8_t param_id, float value);

/**
 * @brief Load a param float value from NVS.
 */
esp_err_t nvs_util_load_param_value(void *h, uint8_t param_id, float *out_value);

/**
 * @brief Dump all NVS entries in a namespace (diagnostic).
 */
void nvs_util_dump_entries(void *h, const char *namespace_name);

#ifdef __cplusplus
}
#endif
