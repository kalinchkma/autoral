/**
 * @file system_init.h
 * @brief This file contains the declarations for system initialization functions.
 */

#pragma once

#include "esp_err.h"

/**
 * @brief Initialize the complete system
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t system_init(void);

/**
 * @brief Initialize the NVS (Non-Volatile Storage) system
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t nvs_init(void);

/**
 * @brief Initialize the GPIO (General Purpose Input/Output) system
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t gpio_init(void);

/**
 * @brief Log system information
 */
void system_log_info(void);
