/**
 * @file app_logging.c
 * @brief This file contains the implementation of the logging system for the application.
 */

#include "app_logging.h"
#include "esp_log.h"
#include <time.h>
#include <stdio.h>
#include <stdarg.h>

void app_logging_init(void)
{
    // Set default logging level to INFO
    esp_log_level_set("*", ESP_LOG_INFO);

    // Initialize the logging system
    APP_LOGI("LOGGING", "Logging system initialized");
}


void app_log_with_timestamp(const char* tag, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    // Get the current timestamp
    time_t now = time(NULL);
    struct tm* timeinfo = localtime(&now);
    char timestamp[20];
    
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);

    // Log the message with the timestamp
    APP_LOGI(tag, "[%s] ", timestamp);
    vprintf(fmt, args);
    printf("\n");

    va_end(args);
}