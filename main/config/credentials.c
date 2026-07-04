/**
 * @file credentials.c
 * @brief NVS-backed credential storage.
 */

#include "credentials.h"
#include "app_logging.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "CRED";

#define NVS_NS_CRED     "cred"
#define NVS_KEY_WIFI    "wifi"
#define NVS_KEY_MQTT    "mqtt"
#define NVS_KEY_FLAGS   "flags"

#define FLAG_WIFI_SET   BIT0
#define FLAG_MQTT_SET   BIT1

static nvs_handle_t s_nvs = 0;
static bool s_inited = false;

/* ────────────────────────────────────────────────────────────────────── */

esp_err_t cred_init(void)
{
    if (s_inited) return ESP_OK;

    esp_err_t err = nvs_open(NVS_NS_CRED, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        APP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }

    s_inited = true;
    APP_LOGI(TAG, "Credential store initialized");
    return ESP_OK;
}

/* ── WiFi ────────────────────────────────────────────────────────────── */

esp_err_t cred_wifi_get(cred_wifi_t *out)
{
    if (!out || !s_inited) return ESP_ERR_INVALID_ARG;

    size_t sz = sizeof(cred_wifi_t);
    esp_err_t err = nvs_get_blob(s_nvs, NVS_KEY_WIFI, out, &sz);
    if (err != ESP_OK) {
        memset(out, 0, sizeof(*out));
        return err;
    }
    return ESP_OK;
}

esp_err_t cred_wifi_set(const cred_wifi_t *cred)
{
    if (!cred || !s_inited) return ESP_ERR_INVALID_ARG;

    esp_err_t err = nvs_set_blob(s_nvs, NVS_KEY_WIFI, cred, sizeof(*cred));
    if (err != ESP_OK) return err;

    /* Set flag */
    uint32_t flags = 0;
    nvs_get_u32(s_nvs, NVS_KEY_FLAGS, &flags);
    flags |= FLAG_WIFI_SET;
    nvs_set_u32(s_nvs, NVS_KEY_FLAGS, flags);

    err = nvs_commit(s_nvs);
    if (err == ESP_OK) {
        APP_LOGI(TAG, "WiFi credentials stored (SSID: %s)", cred->ssid);
    }
    return err;
}

bool cred_wifi_is_set(void)
{
    if (!s_inited) return false;
    uint32_t flags = 0;
    nvs_get_u32(s_nvs, NVS_KEY_FLAGS, &flags);
    return (flags & FLAG_WIFI_SET) != 0;
}

/* ── MQTT ────────────────────────────────────────────────────────────── */

esp_err_t cred_mqtt_get(cred_mqtt_t *out)
{
    if (!out || !s_inited) return ESP_ERR_INVALID_ARG;

    size_t sz = sizeof(cred_mqtt_t);
    esp_err_t err = nvs_get_blob(s_nvs, NVS_KEY_MQTT, out, &sz);
    if (err != ESP_OK) {
        memset(out, 0, sizeof(*out));
        return err;
    }
    return ESP_OK;
}

esp_err_t cred_mqtt_set(const cred_mqtt_t *cred)
{
    if (!cred || !s_inited) return ESP_ERR_INVALID_ARG;

    esp_err_t err = nvs_set_blob(s_nvs, NVS_KEY_MQTT, cred, sizeof(*cred));
    if (err != ESP_OK) return err;

    /* Set flag */
    uint32_t flags = 0;
    nvs_get_u32(s_nvs, NVS_KEY_FLAGS, &flags);
    flags |= FLAG_MQTT_SET;
    nvs_set_u32(s_nvs, NVS_KEY_FLAGS, flags);

    err = nvs_commit(s_nvs);
    if (err == ESP_OK) {
        APP_LOGI(TAG, "MQTT credentials stored (URI: %s)", cred->uri);
    }
    return err;
}

bool cred_mqtt_is_set(void)
{
    if (!s_inited) return false;
    uint32_t flags = 0;
    nvs_get_u32(s_nvs, NVS_KEY_FLAGS, &flags);
    return (flags & FLAG_MQTT_SET) != 0;
}

/* ── Reset ───────────────────────────────────────────────────────────── */

esp_err_t cred_erase_all(void)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;

    nvs_erase_all(s_nvs);
    esp_err_t err = nvs_commit(s_nvs);
    APP_LOGI(TAG, "All credentials erased");
    return err;
}
