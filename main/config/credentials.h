/**
 * @file credentials.h
 * @brief Persistent credential storage for WiFi and MQTT.
 *
 * Credentials are stored in NVS and survive reboots.
 * They can be set at runtime (via BLE Mesh, provisioner, etc.)
 * or pre-loaded with defaults in main.c.
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Limits ──────────────────────────────────────────────────────────── */
#define CRED_WIFI_SSID_MAX      33   /* 32 chars + null */
#define CRED_WIFI_PASS_MAX      65   /* 64 chars + null */
#define CRED_MQTT_URI_MAX       128
#define CRED_MQTT_USER_MAX      65
#define CRED_MQTT_PASS_MAX      65
#define CRED_MQTT_CLIENT_MAX    65
#define CRED_MQTT_TOPIC_MAX     64

/* ── WiFi credentials ────────────────────────────────────────────────── */
typedef struct {
    char ssid[CRED_WIFI_SSID_MAX];
    char password[CRED_WIFI_PASS_MAX];
} cred_wifi_t;

/* ── MQTT credentials ────────────────────────────────────────────────── */
typedef struct {
    char uri[CRED_MQTT_URI_MAX];
    char client_id[CRED_MQTT_CLIENT_MAX];
    char username[CRED_MQTT_USER_MAX];
    char password[CRED_MQTT_PASS_MAX];
    char topic_commands[CRED_MQTT_TOPIC_MAX];
    char topic_status[CRED_MQTT_TOPIC_MAX];
    char topic_telemetry[CRED_MQTT_TOPIC_MAX];
    uint16_t keepalive;
} cred_mqtt_t;

/* ── Lifecycle ───────────────────────────────────────────────────────── */

/** Initialise the credential store (opens NVS). */
esp_err_t cred_init(void);

/* ── WiFi ────────────────────────────────────────────────────────────── */

/** Get the stored WiFi credentials. Returns ESP_ERR_NOT_FOUND if not set. */
esp_err_t cred_wifi_get(cred_wifi_t *out);

/** Store WiFi credentials (persisted to NVS). */
esp_err_t cred_wifi_set(const cred_wifi_t *cred);

/** Check if WiFi credentials have been stored. */
bool cred_wifi_is_set(void);

/* ── MQTT ────────────────────────────────────────────────────────────── */

/** Get the stored MQTT credentials. Returns ESP_ERR_NOT_FOUND if not set. */
esp_err_t cred_mqtt_get(cred_mqtt_t *out);

/** Store MQTT credentials (persisted to NVS). */
esp_err_t cred_mqtt_set(const cred_mqtt_t *cred);

/** Check if MQTT credentials have been stored. */
bool cred_mqtt_is_set(void);

/* ── Reset ───────────────────────────────────────────────────────────── */

/** Erase all stored credentials (factory reset). */
esp_err_t cred_erase_all(void);

#ifdef __cplusplus
}
#endif
