/**
 * @file app_task.h
 * @brief This file contains the application task functions for the system.
 */
#pragma once

#include "esp_err.h"

/**
 * @brief Initializes the application task.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t app_task_init(void);

/**
 * @brief The Wi-Fi task function. Handler Wi-Fi connection and reconnection and status monitoring.
 * @param pvParameters Pointer to the task parameters (not used).
 */
void task_wifi(void *pvParameters);

/**
 * @brief The MQTT task function. Handles MQTT connection, subscription, and message processing.
 * @param pvParameters Pointer to the task parameters (not used).
 */
void task_mqtt(void *pvParameters);


/**
 * @brief Task: Device control. Processes control commands and manages device state.
 * @param pvParameters Pointer to the task parameters (not used).
 */
void task_control(void *pvParameters);

/**
 * @brief Task: Monitoring/Telemetry, Periodically collects and sends telemetry data to the server.
 * @param pvParameters Pointer to the task parameters (not used).
 */
void task_monitor(void *pvParameters);
