/**
 * @file mqtt_manager.c
 * @brief This file contains the implementation of the MQTT manager used in the application.
 */
#include "mqtt_manager.h"
#include "app_logging.h"
#include "app_config.h"
#include "mqtt_client.h"

static const char *TAG = "MQTT_MANAGER";

static esp_mqtt_client_handle_t s_client = NULL;
static mqtt_state_t s_state = MQTT_STATE_DISCONNECTED;

/**
 * @brief Parse and execute the command received via MQTT
 * @param topic The MQTT topic of the received message
 * @param payload The payload of the received message
 * @param len The length of the payload
 */
static void mqtt_process_command(const char *topic, const char *payload, int len) 
{
    /**
     * @todo Implement command parsing and execution logic here.
     * This function should parse the payload and execute the corresponding command.
     * For example, if the payload is "TURN_ON", it should turn on a device
     */
}

/**
 * @brief MQTT event handler
 */

 static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
 {
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    switch ((esp_mqtt_event_id_t)event_id) 
    {
        case MQTT_EVENT_CONNECTED:
            APP_LOGI(TAG, "Connected to MQTT broker");
            s_state = MQTT_STATE_CONNECTED;

            // Subscribe to the command topic
            msg_id = esp_mqtt_client_subscribe(client, MQTT_TOPIC_COMMANDS, 1);
            APP_LOGI(TAG, "Subscribed to topic %s, msg_id=%d", MQTT_TOPIC_COMMANDS, msg_id);

            // Publish online status
            mqtt_publish(MQTT_TOPIC_STATUS, "online", 6, 1);
            break;

        case MQTT_EVENT_DISCONNECTED:
            APP_LOGI(TAG, "Disconnected from MQTT broker");
            s_state = MQTT_STATE_DISCONNECTED;
            break;

        case MQTT_EVENT_SUBSCRIBED:
            // NOTE: event->topic is NULL for SUBSCRIBED events - only msg_id is valid
            APP_LOGI(TAG, "Subscribe acknowledged, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            APP_LOGI(TAG, "Unsubscribe acknowledged, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            // NOTE: event->topic is NULL for PUBLISHED events - only msg_id is valid
            APP_LOGI(TAG, "Publish acknowledged, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            APP_LOGI(TAG, "Received data on topic %s, len=%d", event->topic, event->data_len);
            APP_LOGI(TAG, "Data: %.*s", event->data_len, event->data);
            mqtt_process_command(event->topic, event->data, event->data_len);
            break;

        case MQTT_EVENT_ERROR:
            APP_LOGE(TAG, "MQTT error occurred");
            s_state = MQTT_STATE_ERROR;
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                APP_LOGE(TAG, "Transport error, esp_tls_last_esp_err=%d, esp_tls_stack_err=%d",
                         event->error_handle->esp_tls_last_esp_err,
                         event->error_handle->esp_tls_stack_err);
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                APP_LOGE(TAG, "Connection refused, esp_tls_last_esp_err=%d",
                         event->error_handle->esp_tls_last_esp_err);
            } else {
                APP_LOGE(TAG, "Unknown error type: %d", event->error_handle->error_type);
            }
            break;
        
        case MQTT_EVENT_BEFORE_CONNECT:
            APP_LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT");
            s_state = MQTT_STATE_CONNECTING;
            break;

        default:
            APP_LOGW(TAG, "Other event id:%d", event->event_id);
            break;
    }


 }


 esp_err_t mqtt_init_and_start(void)
 {
    APP_LOGI(TAG, "Initializing MQTT client...");

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .uri = MQTT_BROKER_URI
            }
        },
        .credentials = {
            .client_id = MQTT_CLIENT_ID,
            .username = MQTT_USERNAME,
            .authentication = {
                .password = MQTT_PASSWORD,
            }
        },
        .session = {
            .keepalive = MQTT_KEEPALIVE
        },
        .network = {
            .reconnect_timeout_ms = MQTT_RETRY_INTERVAL_MS,
        }
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) 
    {
        APP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }

    // Register MQTT event handler
    esp_err_t ret = esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (ret != ESP_OK) 
    {
        APP_LOGE(TAG, "Failed to register MQTT event handler");
        return ret;
    }

    // Start MQTT client
    ret = esp_mqtt_client_start(s_client);
    if (ret != ESP_OK)
    {
        APP_LOGE(TAG, "Failed to start MQTT client");
        return ret;
    }

    return ESP_OK;
}


esp_err_t mqtt_stop(void)
{
    if (s_client == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    APP_LOGI(TAG, "Stopping MQTT client...");
    esp_err_t ret = esp_mqtt_client_stop(s_client);
    if (ret != ESP_OK)
    {
        APP_LOGE(TAG, "Failed to stop MQTT client");
        return ret;
    }
    s_state = MQTT_STATE_DISCONNECTED;
    return ESP_OK;
}

bool mqtt_is_connected(void)
{
    return s_state == MQTT_STATE_CONNECTED;
}

mqtt_state_t mqtt_get_state(void)
{
    return s_state;
}

esp_mqtt_client_handle_t mqtt_get_client(void)
{
    return s_client;
}

int mqtt_publish(const char *topic, const char *payload, int len, int qos)
{
    if (s_client == NULL || !mqtt_is_connected())
    {
        APP_LOGE(TAG, "MQTT client is not connected");
        return -1;
    }

    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, len, qos, 0);
    if (msg_id < 0)
    {
        APP_LOGE(TAG, "Failed to publish message to topic %s", topic);
    }
    else 
    {
        APP_LOGI(TAG, "Published message to topic %s, msg_id=%d", topic, msg_id);
    }
    return msg_id;
}

int mqtt_subscribe(const char *topic, int qos)
{
    if (s_client == NULL || !mqtt_is_connected())
    {
        APP_LOGE(TAG, "MQTT client is not connected");
        return -1;
    }

    int msg_id = esp_mqtt_client_subscribe(s_client, topic, qos);
    if (msg_id < 0)
    {
        APP_LOGE(TAG, "Failed to subscribe to topic %s", topic);
    }
    else 
    {
        APP_LOGI(TAG, "Subscribed to topic %s, msg_id=%d", topic, msg_id);
    }
    return msg_id;
}

