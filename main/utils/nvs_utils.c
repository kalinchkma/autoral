/**
 * @file nvs_utils.c
 * @brief Generic NVS utility — implementation.
 *
 * ESP32 NVS (Non-Volatile Storage) is backed by flash memory.
 * Data written here survives power cycles, reboots, and even
 * firmware updates — as long as the NVS partition is not erased.
 *
 * Key facts about ESP32 NVS:
 *   - Partition is typically 20-24 KB (configurable in partition table).
 *   - Supports up to 255 entries per namespace.
 *   - Blob values up to ~500 KB each.
 *   - Uses wear-leveling to extend flash lifetime.
 *   - Data persists without power (flash is non-volatile).
 */

#include "nvs_utils.h"
#include "app_logging.h"

#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "NVS_UTIL";

/* ──────────────────────────────────────────────────────────────────────
 *  Flash initialization
 * ────────────────────────────────────────────────────────────────────── */

esp_err_t nvs_util_flash_init(void)
{
    APP_LOGI(TAG, "Initializing NVS flash...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        APP_LOGW(TAG, "NVS partition issue, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    if (ret == ESP_OK) {
        APP_LOGI(TAG, "NVS flash ready");
    } else {
        APP_LOGE(TAG, "NVS flash init failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/* ──────────────────────────────────────────────────────────────────────
 *  Namespace open / close
 * ────────────────────────────────────────────────────────────────────── */

esp_err_t nvs_util_open(const char *ns, void **out_h)
{
    if (!ns || !out_h) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        APP_LOGE(TAG, "Failed to open namespace '%s': %s",
                 ns, esp_err_to_name(err));
        return err;
    }

    *out_h = (void *)(uintptr_t)h;
    APP_LOGI(TAG, "Opened namespace '%s'", ns);
    return ESP_OK;
}

void nvs_util_close(void *h)
{
    if (h) {
        nvs_close((nvs_handle_t)(uintptr_t)h);
    }
}

/* ──────────────────────────────────────────────────────────────────────
 *  Blob operations
 * ────────────────────────────────────────────────────────────────────── */

esp_err_t nvs_util_set_blob(void *h, const char *key,
                             const void *data, size_t len)
{
    if (!h || !key || !data) return ESP_ERR_INVALID_ARG;

    nvs_handle_t nh = (nvs_handle_t)(uintptr_t)h;
    esp_err_t err = nvs_set_blob(nh, key, data, len);
    if (err != ESP_OK) {
        APP_LOGE(TAG, "set_blob('%s') FAILED: %s (handle=%lu, len=%u)",
                 key, esp_err_to_name(err), (unsigned long)nh, (unsigned)len);
        return err;
    }

    err = nvs_commit(nh);
    if (err != ESP_OK) {
        APP_LOGE(TAG, "commit('%s') FAILED: %s",
                 key, esp_err_to_name(err));
    } else {
        APP_LOGI(TAG, "set_blob('%s') OK, %u bytes committed",
                 key, (unsigned)len);
    }
    return err;
}

esp_err_t nvs_util_get_blob(void *h, const char *key,
                             void *out, size_t *len)
{
    if (!h || !key || !out || !len) return ESP_ERR_INVALID_ARG;

    nvs_handle_t nh = (nvs_handle_t)(uintptr_t)h;
    size_t orig_len = *len;
    esp_err_t err = nvs_get_blob(nh, key, out, len);
    if (err == ESP_OK) {
        APP_LOGI(TAG, "get_blob('%s') OK, %u -> %u bytes",
                 key, (unsigned)orig_len, (unsigned)*len);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        APP_LOGE(TAG, "get_blob('%s') FAILED: %s",
                 key, esp_err_to_name(err));
    }
    return err;
}

/* ──────────────────────────────────────────────────────────────────────
 *  Typed value operations
 * ────────────────────────────────────────────────────────────────────── */

esp_err_t nvs_util_set_u8(void *h, const char *key, uint8_t val)
{
    if (!h || !key) return ESP_ERR_INVALID_ARG;
    nvs_handle_t nh = (nvs_handle_t)(uintptr_t)h;
    esp_err_t err = nvs_set_u8(nh, key, val);
    if (err == ESP_OK) err = nvs_commit(nh);
    return err;
}

esp_err_t nvs_util_get_u8(void *h, const char *key, uint8_t *out)
{
    if (!h || !key || !out) return ESP_ERR_INVALID_ARG;
    nvs_handle_t nh = (nvs_handle_t)(uintptr_t)h;
    return nvs_get_u8(nh, key, out);
}

esp_err_t nvs_util_set_u32(void *h, const char *key, uint32_t val)
{
    if (!h || !key) return ESP_ERR_INVALID_ARG;
    nvs_handle_t nh = (nvs_handle_t)(uintptr_t)h;
    esp_err_t err = nvs_set_u32(nh, key, val);
    if (err == ESP_OK) err = nvs_commit(nh);
    return err;
}

esp_err_t nvs_util_get_u32(void *h, const char *key, uint32_t *out)
{
    if (!h || !key || !out) return ESP_ERR_INVALID_ARG;
    nvs_handle_t nh = (nvs_handle_t)(uintptr_t)h;
    return nvs_get_u32(nh, key, out);
}

esp_err_t nvs_util_set_float(void *h, const char *key, float val)
{
    return nvs_util_set_blob(h, key, &val, sizeof(float));
}

esp_err_t nvs_util_get_float(void *h, const char *key, float *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    size_t sz = sizeof(float);
    return nvs_util_get_blob(h, key, out, &sz);
}

/* ──────────────────────────────────────────────────────────────────────
 *  Delete / existence / count
 * ────────────────────────────────────────────────────────────────────── */

esp_err_t nvs_util_delete(void *h, const char *key)
{
    if (!h || !key) return ESP_ERR_INVALID_ARG;

    nvs_handle_t nh = (nvs_handle_t)(uintptr_t)h;
    esp_err_t err = nvs_erase_key(nh, key);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            APP_LOGE(TAG, "delete('%s') failed: %s",
                     key, esp_err_to_name(err));
        }
        return err;
    }
    err = nvs_commit(nh);
    return err;
}

bool nvs_util_exists(void *h, const char *key)
{
    if (!h || !key) return false;

    nvs_handle_t nh = (nvs_handle_t)(uintptr_t)h;
    /* Try to read; if it succeeds (or data is bigger than 0 bytes), key exists */
    size_t sz = 0;
    esp_err_t err = nvs_get_blob(nh, key, NULL, &sz);
    return (err == ESP_OK || err == ESP_ERR_NVS_INVALID_LENGTH);
}

esp_err_t nvs_util_erase_all(void *h)
{
    if (!h) return ESP_ERR_INVALID_ARG;
    nvs_handle_t nh = (nvs_handle_t)(uintptr_t)h;
    esp_err_t err = nvs_erase_all(nh);
    if (err == ESP_OK) err = nvs_commit(nh);
    return err;
}

uint16_t nvs_util_count(void *h)
{
    (void)h;
    return 0;
}

/* ──────────────────────────────────────────────────────────────────────
 *  Engine-specific NVS persistence
 * ────────────────────────────────────────────────────────────────────── */

#define NVS_KEY_RULE_FMT  "rule_%d"
#define NVS_KEY_RULE_CNT  "rule_cnt"
#define NVS_KEY_PARAM_FMT "param_%d"

#ifndef ENGINE_DEBOUNCED_DEFAULT
#define ENGINE_DEBOUNCED_DEFAULT 5
#endif
#ifndef ENGINE_COOLDOWN_DEFAULT
#define ENGINE_COOLDOWN_DEFAULT 50
#endif

esp_err_t nvs_util_save_rule(void *h, const engine_rule_t *rule)
{
    if (!h || !rule || rule->id == 0) return ESP_ERR_INVALID_ARG;

    char key[16];
    snprintf(key, sizeof(key), NVS_KEY_RULE_FMT, rule->id);
    esp_err_t err = nvs_util_set_blob(h, key, rule, sizeof(*rule));
    if (err == ESP_OK) {
        /* Immediate read-back verification */
        size_t sz = sizeof(engine_rule_t);
        engine_rule_t verify = {0};
        esp_err_t v = nvs_util_get_blob(h, key, &verify, &sz);
        if (v == ESP_OK && verify.id == rule->id) {
            APP_LOGI(TAG, "Rule [%u] '%s' saved + verified (%u bytes)",
                     rule->id, rule->name, (unsigned)sizeof(*rule));
        } else {
            APP_LOGE(TAG, "Rule [%u] save appeared OK but verify FAILED!",
                     rule->id);
        }
    } else {
        APP_LOGE(TAG, "Rule [%u] save FAILED: %s",
                 rule->id, esp_err_to_name(err));
    }
    return err;
}

esp_err_t nvs_util_erase_rule(void *h, uint8_t rule_id)
{
    if (!h || rule_id == 0) return ESP_ERR_INVALID_ARG;
    char key[16];
    snprintf(key, sizeof(key), NVS_KEY_RULE_FMT, rule_id);
    return nvs_util_delete(h, key);
}

void nvs_util_load_rules(void *h, engine_rule_t *out_rules,
                         uint8_t max_count, uint8_t *out_count)
{
    if (!h || !out_rules || !out_count) return;
    *out_count = 0;

    /* Enumerate ALL blob entries and load those prefixed "rule_" */
    nvs_handle_t nh = (nvs_handle_t)(uintptr_t)h;
    nvs_iterator_t it = NULL;
    esp_err_t err = nvs_entry_find_in_handle(nh, NVS_TYPE_BLOB, &it);

    while (err == ESP_OK && it) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);

        if (strncmp(info.key, "rule_", 5) == 0) {
            size_t sz = sizeof(engine_rule_t);
            engine_rule_t tmp = {0};
            if (nvs_util_get_blob(h, info.key, &tmp, &sz) == ESP_OK
                && tmp.id != 0) {
                /* Reset runtime state */
                tmp.last_result    = false;
                tmp.debounce_count = 0;
                tmp.cooldown_count = 0;
                if (tmp.type == ENGINE_RULE_DEBOUNCED && tmp.debounce_limit == 0)
                    tmp.debounce_limit = ENGINE_DEBOUNCED_DEFAULT;
                if (tmp.type == ENGINE_RULE_COOLDOWN && tmp.cooldown_limit == 0)
                    tmp.cooldown_limit = ENGINE_COOLDOWN_DEFAULT;
                if (*out_count < max_count) {
                    out_rules[(*out_count)++] = tmp;
                    APP_LOGI(TAG, "  Loaded rule [%u] '%s' from '%s'",
                             tmp.id, tmp.name, info.key);
                } else {
                    APP_LOGW(TAG, "  Skipped rule [%u] '%s' — full",
                             tmp.id, tmp.name);
                }
            }
        }
        err = nvs_entry_next(&it);
    }
    if (it) nvs_release_iterator(it);
}

esp_err_t nvs_util_save_param_value(void *h, uint8_t param_id, float value)
{
    if (!h || param_id == 0) return ESP_ERR_INVALID_ARG;
    char key[16];
    snprintf(key, sizeof(key), NVS_KEY_PARAM_FMT, param_id);
    return nvs_util_set_float(h, key, value);
}

esp_err_t nvs_util_load_param_value(void *h, uint8_t param_id, float *out_value)
{
    if (!h || param_id == 0 || !out_value) return ESP_ERR_INVALID_ARG;
    char key[16];
    snprintf(key, sizeof(key), NVS_KEY_PARAM_FMT, param_id);
    return nvs_util_get_float(h, key, out_value);
}

void nvs_util_dump_entries(void *h, const char *namespace_name)
{
    if (!h) return;
    nvs_handle_t nh = (nvs_handle_t)(uintptr_t)h;
    nvs_iterator_t it = NULL;
    esp_err_t err = nvs_entry_find_in_handle(nh, NVS_TYPE_ANY, &it);
    APP_LOGI(TAG, "NVS dump of '%s':", namespace_name ? namespace_name : "?");
    int count = 0;
    while (err == ESP_OK && it) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        APP_LOGI(TAG, "  key='%s' type=%d", info.key, info.type);
        count++;
        err = nvs_entry_next(&it);
    }
    if (it) nvs_release_iterator(it);
    APP_LOGI(TAG, "NVS dump: %d entries", count);
}
