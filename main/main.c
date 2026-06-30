#include <stdio.h>
#include "app_logging.h"
#include "system/system_init.h"
#include "connectivity/wifi_manager.h"
#include "connectivity/mqtt_manager.h"
#include "config/app_config.h"

static const char* TAG = "MAIN";


void app_main(void)
{   
    APP_LOGI(TAG, "=====================================================================");
    APP_LOGI(TAG, "Application started");
    APP_LOGI(TAG, "=====================================================================");


    // =========================================================================
    // Initialize logging
    // =========================================================================
    app_logging_init();

    // =========================================================================
    // Initialize system (NVS, GPIO, etc.)
    // =========================================================================
    esp_err_t ret = system_init();
    if (ret != ESP_OK) {
        APP_LOGE(TAG, "System initialization failed");
        return;
    }

    // =========================================================================
    // Initialize WIFI
    // =========================================================================
    ret = wifi_init_sta();
    if (ret != ESP_OK) {
        APP_LOGE(TAG, "WIFI initialization failed");
        return;
    }

    // =========================================================================
    // Wait for WIFI connection before starting MQTT
    // =========================================================================
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    EventBits_t bits = xEventGroupWaitBits(
        wifi_get_event_group(),
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS)
    );

    if (!(bits & WIFI_CONNECTED_BIT)) {
        APP_LOGE(TAG, "WiFi connection timed out");
        return;
    }

    APP_LOGI(TAG, "WiFi connected, starting MQTT...");

    // =========================================================================
    // Initialize MQTT
    // =========================================================================
    ret = mqtt_init_and_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize MQTT: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }
}
