/**
 * @file wifi_manager.h
 * @brief Wifi manager handlers wifi connection and events
 */

#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

// Event group bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_DISCONNECTED_BIT BIT1

/**
 * @brief Initialize WIFI in station mode
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t wifi_init_sta(void);


/**
 * @brief Disconnect from the configured WIFI network
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t wifi_disconnect(void);

/**
 * @brief Check if the device is connected to WIFI
 * @return true if connected, false otherwise
 */
bool wifi_is_connected(void);

/**
 * @brief Get the event group for WIFI events
 * @return Event group handle
 */
EventGroupHandle_t wifi_get_event_group(void);

/**
 * @brief Get the IP address of the device
 * @param ip_buffer A buffer to store the IP address as a string (must be at least 16 bytes)
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t wifi_get_ip_address(char* ip_buffer);

/**
 * @brief Reconnect to the configured WIFI network
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t wifi_reconnect(void);