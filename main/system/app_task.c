/**
 * @file app_task.c
 * @brief Application tasks - all business logic lives here.
 *
 * - task_wifi:    Monitors Wi-Fi status (reconnection handled by wifi_manager)
 * - task_mqtt:    Waits on MQTT message queue, processes commands
 * - task_control: Placeholder for device control logic
 * - task_monitor: Placeholder for telemetry collection
 */
#include "app_task.h"
#include "app_config.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "gpio_control.h"
#include "control/engine.h"
#include "config/credentials.h"
#include "connectivity/ble_mesh_manager.h"
#include "app_logging.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "TASKS";

static TaskHandle_t h_task_wifi = NULL;
static TaskHandle_t h_task_mqtt = NULL;
static TaskHandle_t h_task_control = NULL;
static TaskHandle_t h_task_monitor = NULL;

// =========================================================================
// Wi-Fi Monitoring Task (status only - reconnection handled by wifi_manager)
// =========================================================================

void task_wifi(void *pvParameters)
{
    APP_LOGI(TAG, "Wi-Fi task started.");

    EventGroupHandle_t wifi_event_group = wifi_get_event_group();

    while (1)
    {
        /* Wait for WiFi state changes. Timeout every 5s to check status.
         * pdTRUE clears the bit on exit so we don't spin in a tight loop. */
        EventBits_t bits = xEventGroupWaitBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_DISCONNECTED_BIT,
            pdTRUE,   /* clear on exit — prevents tight-loop starvation */
            pdFALSE,
            pdMS_TO_TICKS(5000)
        );

        if (bits & WIFI_CONNECTED_BIT) {
            /* IP address is already logged by the WiFi event handler */
        }

        if (bits & WIFI_DISCONNECTED_BIT) {
            APP_LOGW(TAG, "Wi-Fi disconnected - reconnecting managed by wifi_manager");
        }

        /* Always yield so IDLE task on CPU1 can run */
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// =========================================================================
// MQTT Command Processing (business logic)
// =========================================================================

/**
 * @brief Process a single MQTT command message.
 *
 * This is where YOUR business logic lives. Extend this function to handle
 * commands from your server (GPIO control, servo movement, config changes, etc.).
 *
 * @param topic     The topic the message was received on
 * @param payload   The command payload
 * @param len       Length of the payload
 */
static void mqtt_process_command(const char *topic, const char *payload, int len)
{
    APP_LOGI(TAG, "Processing command on '%s': %.*s", topic, len, payload);

    /* ── Engine rule commands ────────────────────────────────────────── */
    if (strstr(topic, "engine/rules/set")) {
        APP_LOGI(TAG, "Rule SET received (%d bytes): %.*s", len, len > 300 ? 300 : len, payload);
        engine_rule_t rule = {0};
        esp_err_t parse_err = engine_rule_from_json(&rule, payload, len);
        APP_LOGI(TAG, "JSON parse result: %s, rule.id=%u, rule.name='%s'",
                 esp_err_to_name(parse_err), rule.id, rule.name);
        if (parse_err == ESP_OK && rule.id != 0) {
            if (engine_rule_get(rule.id)) {
                engine_rule_update(rule.id, &rule);
                APP_LOGI(TAG, "Updated rule [%u] '%s'", rule.id, rule.name);
            } else {
                engine_rule_create(&rule);
                APP_LOGI(TAG, "Created rule [%u] '%s'", rule.id, rule.name);
            }
            char buf[512];
            if (engine_rule_to_json(&rule, buf, sizeof(buf)) == ESP_OK) {
                cred_mqtt_t mc = {0};
                cred_mqtt_get(&mc);
                mqtt_publish(mc.topic_status, buf, strlen(buf), 1);
            }
        } else {
            APP_LOGE(TAG, "Failed to parse rule JSON (err=%s, id=%u)", esp_err_to_name(parse_err), rule.id);
        }
        return;
    }

    if (strstr(topic, "engine/rules/delete")) {
        APP_LOGI(TAG, "Rule DELETE received: %.*s", len, payload);
        /* Extract id from JSON: {"id": N} */
        const char *p = strstr(payload, "\"id\":");
        if (p) {
            uint8_t id = (uint8_t)atoi(p + 5);
            esp_err_t del_err = engine_rule_delete(id);
            APP_LOGI(TAG, "Rule delete result: %s (id=%u, remaining=%u)",
                     esp_err_to_name(del_err), id, engine_rule_get_count());
        } else {
            APP_LOGE(TAG, "Rule DELETE: no 'id' found in payload");
        }
        return;
    }

    if (strstr(topic, "engine/rules/get")) {
        char buf[2048];
        if (engine_export_all_json(buf, sizeof(buf)) == ESP_OK) {
            cred_mqtt_t mc = {0};
            cred_mqtt_get(&mc);
            mqtt_publish(mc.topic_status, buf, strlen(buf), 0);
        }
        return;
    }

    /* ── Legacy PIN commands ─────────────────────────────────────────── */
    if (strncmp(payload, "PIN", 3) == 0) {
        int pin = 0;
        bool turn_on = false;

        if (sscanf(payload, "PIN%d_ON", &pin) == 1) {
            turn_on = true;
        } else if (sscanf(payload, "PIN%d_OFF", &pin) == 1) {
            turn_on = false;
        }

        if (pin > 0) {
            esp_err_t err = gpio_digital_write(pin, turn_on ? PIN_HIGH : PIN_LOW);
            if (err == ESP_OK) {
                APP_LOGI(TAG, "Set pin %d to %s", pin, turn_on ? "HIGH" : "LOW");
                char msg[32];
                snprintf(msg, sizeof(msg), "PIN%d_%s", pin, turn_on ? "ON" : "OFF");
                cred_mqtt_t mc = {0};
                cred_mqtt_get(&mc);
                mqtt_publish(mc.topic_status, msg, strlen(msg), 1);
            } else {
                APP_LOGE(TAG, "Failed to set pin %d: %s", pin, esp_err_to_name(err));
            }
        }
    }
    else {
        APP_LOGW(TAG, "Unknown command: %.*s", len, payload);
    }
}

// =========================================================================
// BLE Mesh Data Handler (processes incoming mesh packets as engine rules)
// =========================================================================

/**
 * @brief Callback registered with BLE Mesh for incoming data.
 *
 * Expects JSON payloads with the same format as MQTT engine commands:
 *   - {"type":"rule_set", ...}  → create/update rule
 *   - {"type":"rule_delete", "id": N} → delete rule
 *   - {"type":"rule_get"}       → respond with all state
 *   - {"type":"cred_wifi", "ssid":"...", "password":"..."} → set WiFi creds
 *   - {"type":"cred_mqtt", "uri":"...", ...} → set MQTT creds
 */
static void mesh_data_handler(uint16_t src_addr, const uint8_t *data, size_t len)
{
    /* Ensure null-terminated string for JSON parsing */
    char *json = malloc(len + 1);
    if (!json) return;
    memcpy(json, data, len);
    json[len] = '\0';

    APP_LOGI(TAG, "Mesh packet from 0x%04x (%u bytes): %.*s", src_addr, len, len, json);

    /* ── Rule commands ──────────────────────────────────────────────── */
    if (strstr(json, "\"rule_set\"")) {
        engine_rule_t rule = {0};
        if (engine_rule_from_json(&rule, json, len) == ESP_OK && rule.id != 0) {
            if (engine_rule_get(rule.id)) {
                engine_rule_update(rule.id, &rule);
            } else {
                engine_rule_create(&rule);
            }
            APP_LOGI(TAG, "Mesh: rule [%u] '%s' saved", rule.id, rule.name);
        }
    }
    else if (strstr(json, "\"rule_delete\"")) {
        const char *p = strstr(json, "\"id\":");
        if (p) {
            uint8_t id = (uint8_t)atoi(p + 5);
            engine_rule_delete(id);
            APP_LOGI(TAG, "Mesh: rule [%u] deleted", id);
        }
    }
    else if (strstr(json, "\"rule_get\"")) {
        /* Respond with full state via mesh */
        char buf[1024];
        if (engine_export_all_json(buf, sizeof(buf)) == ESP_OK) {
            mesh_send(src_addr, (const uint8_t *)buf, strlen(buf));
        }
    }
    /* ── Credential commands (provisioning via BLE Mesh) ────────────── */
    else if (strstr(json, "\"cred_wifi\"")) {
        cred_wifi_t wc = {0};
        const char *p;
        p = strstr(json, "\"ssid\":\"");
        if (p) { p += 8; int i = 0; while (*p && *p != '"' && i < CRED_WIFI_SSID_MAX - 1) wc.ssid[i++] = *p++; }
        p = strstr(json, "\"password\":\"");
        if (p) { p += 12; int i = 0; while (*p && *p != '"' && i < CRED_WIFI_PASS_MAX - 1) wc.password[i++] = *p++; }
        if (wc.ssid[0]) {
            cred_wifi_set(&wc);
            APP_LOGI(TAG, "Mesh: WiFi credentials updated (SSID: %s)", wc.ssid);
        }
    }
    else if (strstr(json, "\"cred_mqtt\"")) {
        cred_mqtt_t mqtt_cred = {0};
        const char *p;
        p = strstr(json, "\"uri\":\"");
        if (p) { p += 7; int i = 0; while (*p && *p != '"' && i < CRED_MQTT_URI_MAX - 1) mqtt_cred.uri[i++] = *p++; }
        p = strstr(json, "\"client_id\":\"");
        if (p) { p += 13; int i = 0; while (*p && *p != '"' && i < CRED_MQTT_CLIENT_MAX - 1) mqtt_cred.client_id[i++] = *p++; }
        p = strstr(json, "\"username\":\"");
        if (p) { p += 12; int i = 0; while (*p && *p != '"' && i < CRED_MQTT_USER_MAX - 1) mqtt_cred.username[i++] = *p++; }
        p = strstr(json, "\"password\":\"");
        if (p) { p += 12; int i = 0; while (*p && *p != '"' && i < CRED_MQTT_PASS_MAX - 1) mqtt_cred.password[i++] = *p++; }
        if (mqtt_cred.uri[0]) {
            mqtt_cred.keepalive = MQTT_KEEPALIVE;
            strncpy(mqtt_cred.topic_commands,  MQTT_TOPIC_COMMANDS,  sizeof(mqtt_cred.topic_commands) - 1);
            strncpy(mqtt_cred.topic_status,    MQTT_TOPIC_STATUS,    sizeof(mqtt_cred.topic_status) - 1);
            strncpy(mqtt_cred.topic_telemetry, MQTT_TOPIC_TELEMETRY, sizeof(mqtt_cred.topic_telemetry) - 1);
            cred_mqtt_set(&mqtt_cred);
            APP_LOGI(TAG, "Mesh: MQTT credentials updated (URI: %s)", mqtt_cred.uri);
        }
    }
    else {
        APP_LOGW(TAG, "Mesh: unknown payload");
    }

    free(json);
}

// =========================================================================
// MQTT Task - reads from queue and dispatches to business logic
// =========================================================================

void task_mqtt(void *pvParameters)
{
    APP_LOGI(TAG, "MQTT task started.");

    // Wait for Wi-Fi before starting MQTT
    EventGroupHandle_t wifi_event_group = wifi_get_event_group();
    APP_LOGI(TAG, "Waiting for Wi-Fi connection...");

    xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY
    );

    APP_LOGI(TAG, "Wi-Fi connected - initializing MQTT...");

    esp_err_t err = mqtt_init_and_start();
    if (err != ESP_OK) {
        APP_LOGE(TAG, "Failed to initialize MQTT: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    // Get the message queue from mqtt_manager
    QueueHandle_t msg_queue = mqtt_get_message_queue();
    if (msg_queue == NULL) {
        APP_LOGE(TAG, "MQTT message queue not available");
        vTaskDelete(NULL);
        return;
    }

    mqtt_msg_t msg;

    // Main loop - block on queue, process messages
    while (1)
    {
        if (xQueueReceive(msg_queue, &msg, portMAX_DELAY) == pdTRUE)
        {
            mqtt_process_command(msg.topic, msg.payload, msg.payload_len);

            // Free heap-allocated copies
            free(msg.topic);
            free(msg.payload);
        }
    }
}

// =========================================================================
// Control Task (placeholder)
// =========================================================================

void task_control(void *pvParameters)
{
    APP_LOGI(TAG, "Control task started.");

    while (1)
    {
        engine_eval_loop();
    }
}

// =========================================================================
// Monitor / Telemetry Task (placeholder)
// =========================================================================

void task_monitor(void *pvParameters)
{
    APP_LOGI(TAG, "Monitor task started.");

    /* Wait for MQTT to be ready */
    vTaskDelay(pdMS_TO_TICKS(3000));

    char buf[2048];
    uint32_t cycle = 0;
    while (1)
    {
        cycle++;
        if (!mqtt_is_connected()) {
            if (cycle % 6 == 0) {  /* Log every 30s to avoid spam */
                APP_LOGW(TAG, "Monitor: MQTT not connected (cycle %lu)", cycle);
            }
        } else {
            esp_err_t err = engine_export_all_json(buf, sizeof(buf));
            if (err != ESP_OK) {
                APP_LOGE(TAG, "Monitor: engine_export_all_json failed: %s", esp_err_to_name(err));
            } else {
                int len = strlen(buf);
                if (len > 0 && len < 200) {
                    APP_LOGI(TAG, "Monitor: telemetry (%d bytes): %s", len, buf);
                } else if (len >= 200) {
                    APP_LOGI(TAG, "Monitor: telemetry (%d bytes)", len);
                }
                mqtt_publish(MQTT_TOPIC_TELEMETRY, buf, len, 0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// =========================================================================
// Task Initialization
// =========================================================================

esp_err_t app_task_init(void)
{
    if (xTaskCreate(task_wifi, "task_wifi", WIFI_TASK_STACK_SIZE, NULL,
                    WIFI_TASK_PRIORITY, &h_task_wifi) != pdPASS) {
        APP_LOGE(TAG, "Failed to create Wi-Fi task.");
        return ESP_FAIL;
    }

    if (xTaskCreate(task_mqtt, "task_mqtt", MQTT_TASK_STACK_SIZE, NULL,
                    MQTT_TASK_PRIORITY, &h_task_mqtt) != pdPASS) {
        APP_LOGE(TAG, "Failed to create MQTT task.");
        return ESP_FAIL;
    }

    if (xTaskCreate(task_control, "task_control", CONTROL_TASK_STACK_SIZE, NULL,
                    CONTROL_TASK_PRIORITY, &h_task_control) != pdPASS) {
        APP_LOGE(TAG, "Failed to create Control task.");
        return ESP_FAIL;
    }

    if (xTaskCreate(task_monitor, "task_monitor", 4096, NULL, 5,
                    &h_task_monitor) != pdPASS) {
        APP_LOGE(TAG, "Failed to create Monitor task.");
        return ESP_FAIL;
    }

    /* Register BLE Mesh data handler for engine rules + credentials */
    mesh_register_data_cb(mesh_data_handler);
    APP_LOGI(TAG, "Mesh data handler registered");

    return ESP_OK;
}