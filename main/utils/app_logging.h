/**
 * @file app_logging.h
 * @brief This file contains the implementation of the logging system for the application.
 */

#pragma once

#include "esp_log.h"

// Logging macros 
#define APP_LOGI(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#define APP_LOGW(tag, fmt, ...) ESP_LOGW(tag, fmt, ##__VA_ARGS__)
#define APP_LOGE(tag, fmt, ...) ESP_LOGE(tag, fmt, ##__VA_ARGS__)
#define APP_LOGD(tag, fmt, ...) ESP_LOGD(tag, fmt, ##__VA_ARGS__)
#define APP_LOGV(tag, fmt, ...) ESP_LOGV(tag, fmt, ##__VA_ARGS__)

/**
 * @brief Initialize the logging system for the application.
 */
void app_logging_init(void);

/**
 * @brief Log a message with a timestamp.
 */
void app_log_with_timestamp(const char* tag, const char* fmt, ...);