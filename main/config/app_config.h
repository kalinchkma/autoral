#pragma once

#include "stdio.h"

// =================================
// Hardware Information & VERSION
// =================================
#define APP_NAME "Autoral"
#define APP_VERSION "1.0.0"
#define DEVICE_TYPE "ESP32"

// =================================
// TASK CONFIGURATION
// =================================
#define WIFI_TASK_PRIORITY 5
#define MQTT_TASK_PRIORITY 5
#define CONTROL_TASK_PRIORITY 6
#define LOGGING_TASK_PRIORITY 3

#define WIFI_TASK_STACK_SIZE (4096)
#define MQTT_TASK_STACK_SIZE (4096)
#define CONTROL_TASK_STACK_SIZE (2048)

// =================================
// STORAGE & NVS CONFIGURATION
// =================================
#define NVS_NAMESPACE "autoral_nvs"
#define NVS_PARTITION "nvs"

// =================================
// OTA CONFIGURATION
// =================================
#define OTA_ENABLE 0
#define OTA_SERVER_URL "http://example.com/ota"

// ================================
// PERFORMANCE TUNING
// ================================
#define TICK_PERIOD_MS (1000 / portTICK_PERIOD_MS)  // 1 second tick period


/**
 * Temporary configuration for the application. This file is intended to be modified by the user to suit their specific needs. It contains various settings related to hardware, tasks, storage, OTA updates, and performance tuning.
 */

// ================================
// Wifi Configuration
// ================================
#define WIFI_SSID "MARUF_SUPER_HOSTEL"
#define WIFI_PASSWORD "maruf404"
#define WIFI_MAX_RETRIES 5
#define WIFI_CONNECT_TIMEOUT_MS 15000

// ===============================
// MQTT Configuration
// ===============================
// #define MQTT_BROKER_URI "mqtt://broker.hivemq.com"
#define MQTT_BROKER_URI "mqtt://your-server.com"
#define MQTT_TOPIC_COMMANDS "autoral_client"
#define MQTT_TOPIC_STATUS "autoral_status"
#define MQTT_TOPIC_TELEMETRY "autoral_telemetry"
#define MQTT_USERNAME "mqtt_user"
#define MQTT_PASSWORD "mqtt_password"
#define MQTT_CLIENT_ID "autoral_client_001"
#define MQTT_KEEPALIVE 60
#define MQTT_RETRY_INTERVAL_MS 5000
#define MQTT_MSG_QUEUE_SIZE 10

