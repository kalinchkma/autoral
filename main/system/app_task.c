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
        EventBits_t bits = xEventGroupWaitBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_DISCONNECTED_BIT,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(5000)
        );

        if (bits & WIFI_CONNECTED_BIT) {
            char ip_str[16];
            if (wifi_get_ip_address(ip_str) == ESP_OK) {
                APP_LOGI(TAG, "Wi-Fi connected - IP: %s", ip_str);
            }
        }

        if (bits & WIFI_DISCONNECTED_BIT) {
            APP_LOGW(TAG, "Wi-Fi disconnected - reconnecting managed by wifi_manager");
        }
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

    // ------------------------------------------------------------------
    // Example: simple string-match commands
    // Extend with your actual command set.
    // ------------------------------------------------------------------

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
                mqtt_publish(MQTT_TOPIC_STATUS, msg, strlen(msg), 1);
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
        // TODO: Process control commands from internal queues
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// =========================================================================
// Monitor / Telemetry Task (placeholder)
// =========================================================================

void task_monitor(void *pvParameters)
{
    APP_LOGI(TAG, "Monitor task started.");

    while (1)
    {
        // TODO: Collect telemetry and publish via mqtt_publish()
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

    return ESP_OK;
}