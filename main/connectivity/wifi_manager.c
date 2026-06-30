/**
 * @file wifi_manager.c
 * @brief Wifi manager handles wifi connection and events
 */

#include "wifi_manager.h"
#include "config/app_config.h"
#include "utils/app_logging.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "freertos/task.h"
#include "string.h"
#include "esp_netif.h"

static const char *TAG = "WIFI";

static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_netif_sta;
static char s_ip_address[16] = {0};  // Buffer to hold the IP address as a string

// Wifi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        APP_LOGI(TAG, "WIFI_EVENT_STA_START: Connecting to AP...");
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        APP_LOGI(TAG, "WiFi disconnected, attempting to reconnect...");
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT);
        esp_wifi_connect();
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;

        // Store the IP address as a string
        snprintf(s_ip_address, sizeof(s_ip_address), IPSTR, IP2STR(&event->ip_info.ip));

        APP_LOGI(TAG, "WiFi Connected, IP address: %s", s_ip_address);

        xEventGroupClearBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }

}


esp_err_t wifi_init_sta(void)
{
    APP_LOGI(TAG, "Initializing WIFI in station mode...");

    // Create event group
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        APP_LOGE(TAG, "Failed to create wifi event group");
        return ESP_ERR_NO_MEM;
    }

    // Initialize network interface
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create wifi STA interface
    s_netif_sta = esp_netif_create_default_wifi_sta();
    if (s_netif_sta == NULL) {
        APP_LOGE(TAG, "Failed to create default wifi STA interface");
        return ESP_FAIL;
    }

    // Initialize wifi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register Wifi event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // Set Wifi configuration
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        }
    };

    // Set Wifi mode and configuration
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // Start Wifi
    ESP_ERROR_CHECK(esp_wifi_start());

    APP_LOGI(TAG, "WIFI initialized in station mode completed");
    return ESP_OK;
}


esp_err_t wifi_disconnect(void)
{
    APP_LOGI(TAG, "Disconnecting from WIFI...");
    esp_err_t ret = esp_wifi_disconnect();
    if (ret == ESP_OK) {
        APP_LOGI(TAG, "Disconnected from WIFI");
    } else {
        APP_LOGE(TAG, "Failed to disconnect from WIFI: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t wifi_reconnect(void)
{
    APP_LOGI(TAG, "Reconnecting to WIFI...");
    esp_err_t ret = esp_wifi_connect();
    if (ret == ESP_OK) {
        APP_LOGI(TAG, "Reconnected to WIFI");
    } else {
        APP_LOGE(TAG, "Failed to reconnect to WIFI: %s", esp_err_to_name(ret));
    }
    return ret;
}

EventGroupHandle_t wifi_get_event_group(void)
{
    return s_wifi_event_group;
}

esp_err_t wifi_get_ip_address(char* ip_buffer)
{
    if (ip_buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Get the IP address from the network interface
    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(s_netif_sta, &ip_info);
    if (ret != ESP_OK) {
        APP_LOGE(TAG, "Failed to get IP address: %s", esp_err_to_name(ret));
        return ret;
    }

    // Convert IP address to string
    snprintf(ip_buffer, 16, IPSTR, IP2STR(&ip_info.ip));

    return ESP_OK;
}