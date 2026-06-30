/**
 * @file mqtt_manager.h
 * @brief This file contains the declarations for the MQTT manager used in the application.
 */

 #pragma once

 #include "esp_err.h"
 #include "mqtt_client.h"

typedef enum {
    MQTT_STATE_DISCONNECTED = 0,
    MQTT_STATE_CONNECTING = 1,
    MQTT_STATE_CONNECTED = 2,
    MQTT_STATE_ERROR = 3
} mqtt_state_t;


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
 * @brief Publish a message to the specified MQTT topic
 * @param topic The MQTT topic to publish to
 * @param payload The message payload to publish
 * @param len The length of the payload
 * @param qos The Quality of Service level for the message (0, 1, or 2)
 * @return Message ID on success, or -1 on failure
 */
int mqtt_publish(const char *topic, const char *payload, int len, int qos);


/**
 * @brief Get the current state of the MQTT client
 * @return The current state of the MQTT client as an mqtt_state_t enum value
 */
mqtt_state_t mqtt_get_state(void);

/**
 * @brief Get the MQTT client handle
 * @return The MQTT client handle, or NULL if the client is not initialized
 */
esp_mqtt_client_handle_t mqtt_get_client(void);

