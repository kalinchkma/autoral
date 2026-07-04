/**
 * @file system_init.c
 * @brief This file contains the implementation of the system initialization functions.
 */

#include "system_init.h"
#include "app_logging.h"
#include "utils/nvs_utils.h"
#include "esp_flash.h"
#include "esp_chip_info.h"
#include "app_config.h"

static const char* TAG = "SYSTEM_INIT";

esp_err_t nvs_init(void)
{
    return nvs_util_flash_init();
}

esp_err_t gpio_init(void)
{
    APP_LOGI(TAG, "Initializing GPIO (General Purpose Input/Output)...");

    // @TODO set all GPIOs to a safe state (input, no pull-up/down) to avoid floating pins

    return ESP_OK;
}

esp_err_t system_init(void)
{
    APP_LOGI(TAG, "Starting system initialization...");

    // Initialize NVS
    esp_err_t ret = nvs_init();
    if (ret != ESP_OK) {
        APP_LOGE(TAG, "NVS initialization failed");
        return ret;
    }

    // Initialize GPIO
    ret = gpio_init();
    if (ret != ESP_OK) {
        APP_LOGE(TAG, "GPIO initialization failed");
        return ret;
    }

    // print system information
    system_log_info();

    APP_LOGI(TAG, "System initialization completed successfully");
    return ESP_OK;
}

void system_log_info(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    APP_LOGI(TAG, "");
    APP_LOGI(TAG, "=====================================================================");
    APP_LOGI(TAG, "               %s v%s", APP_NAME, APP_VERSION);
    APP_LOGI(TAG, "=====================================================================");
    APP_LOGI(TAG, "Chip model: %s", CONFIG_IDF_TARGET);
    APP_LOGI(TAG, "Chip revision: %d", chip_info.revision);

    // Get flash size
    uint32_t flash_size_bytes = 0;
    if (esp_flash_get_size(NULL, &flash_size_bytes) == ESP_OK) {
        APP_LOGI(TAG, "Flash size: %luMB", flash_size_bytes / (1024 * 1024));
    } else {
        APP_LOGE(TAG, "Failed to get flash chip size");
    }
    
    APP_LOGI(TAG, "Flash type: %s",
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    APP_LOGI(TAG, "Features: %s%s%s%s%s",
             (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi " : "",
             (chip_info.features & CHIP_FEATURE_BT) ? "BT " : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "BLE " : "",
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "Flash " : "",
             (chip_info.features & CHIP_FEATURE_EMB_PSRAM) ? "PSRAM " : "");
    APP_LOGI(TAG, "Cores: %d", chip_info.cores);
    APP_LOGI(TAG, "=====================================================================");
    APP_LOGI(TAG, "");
}