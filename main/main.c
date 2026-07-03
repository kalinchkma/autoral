#include <stdio.h>
#include "app_logging.h"
#include "system/system_init.h"
#include "connectivity/wifi_manager.h"
#include "connectivity/mqtt_manager.h"
#include "config/app_config.h"
#include "system/app_task.h"
#include "connectivity/ble_mesh_manager.h"

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
    // Initialize BLE Mesh
    // =========================================================================
    ret = mesh_init();
    if (ret != ESP_OK) 
    {
        APP_LOGE(TAG, "Failed to initialized mesh");
        return;
    }

    // Initialize application tasks
    ret = app_task_init();
    if (ret != ESP_OK) {
        APP_LOGE(TAG, "Failed to initialize application tasks");
        return;
    }
}
