/**
 * @file mqtt_manager.h
 * @brief MQTT transport layer - connection, subscription, publishing, and message queuing.
 * 
 * This module handles ONLY MQTT connectivity. Business logic (command processing)
 * is handled by the application task via the message queue.
 */
#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "mqtt_client.h"

// =========================================================================
// Types
// =========================================================================

typedef enum {
    MQTT_STATE_DISCONNECTED = 0,
    MQTT_STATE_CONNECTING = 1,
    MQTT_STATE_CONNECTED = 2,
    MQTT_STATE_ERROR = 3
} mqtt_state_t;

/**
 * @brief Received MQTT message structure.
 * The caller must free payload after processing.
 */
typedef struct {
    char *topic;        /**< Topic string (heap-allocated copy, caller must free) */
    char *payload;      /**< Payload data (heap-allocated copy, caller must free) */
    int topic_len;      /**< Length of the topic */
    int payload_len;    /**< Length of the payload */
} mqtt_msg_t;

// =========================================================================
// Connection Management
// =========================================================================

/**
 * @brief Initialize and start the MQTT client
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t mqtt_init_and_start(void);

/**
 * @brief Stop the MQTT client
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t mqtt_stop(void);

/**
 * @brief Check if the MQTT client is connected
 * @return true if connected, false otherwise
 */
bool mqtt_is_connected(void);

/**
 * @brief Get the current state of the MQTT client
 * @return The current state as an mqtt_state_t enum value
 */
mqtt_state_t mqtt_get_state(void);

/**
 * @brief Get the MQTT client handle
 * @return The MQTT client handle, or NULL if not initialized
 */
esp_mqtt_client_handle_t mqtt_get_client(void);

// =========================================================================
// Publishing
// =========================================================================

/**
 * @brief Publish a message to the specified MQTT topic
 * @param topic The MQTT topic to publish to
 * @param payload The message payload
 * @param len The length of the payload
 * @param qos Quality of Service level (0, 1, or 2)
 * @return Message ID on success, or -1 on failure
 */
int mqtt_publish(const char *topic, const char *payload, int len, int qos);

// =========================================================================
// Message Queue (for business logic in app_task)
// =========================================================================

/**
 * @brief Get the message queue handle for received MQTT messages.
 * 
 * The application task should block on this queue to receive messages.
 * Each item is an mqtt_msg_t - the caller must free msg.payload after use.
 * 
 * @return Queue handle, or NULL if not initialized
 */
QueueHandle_t mqtt_get_message_queue(void);

