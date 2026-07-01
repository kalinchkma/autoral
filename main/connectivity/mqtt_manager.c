/**
 * @file mqtt_manager.c
 * @brief MQTT transport layer - connection management and message queuing.
 * 
 * This module does NOT process commands. Received messages are pushed to a
 * FreeRTOS queue for the application task to handle.
 */
#include "mqtt_manager.h"
#include "app_logging.h"
#include "app_config.h"
#include "mqtt_client.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "MQTT_MANAGER";

static esp_mqtt_client_handle_t s_client = NULL;
static mqtt_state_t s_state = MQTT_STATE_DISCONNECTED;
static QueueHandle_t s_msg_queue = NULL;

// =========================================================================
// MQTT Event Handler (transport only - no business logic)
// =========================================================================

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    switch ((esp_mqtt_event_id_t)event_id)
    {
        case MQTT_EVENT_CONNECTED:
            APP_LOGI(TAG, "Connected to MQTT broker");
            s_state = MQTT_STATE_CONNECTED;

            msg_id = esp_mqtt_client_subscribe(client, MQTT_TOPIC_COMMANDS, 1);
            APP_LOGI(TAG, "Subscribed to topic %s, msg_id=%d", MQTT_TOPIC_COMMANDS, msg_id);

            mqtt_publish(MQTT_TOPIC_STATUS, "online", 6, 1);
            break;

        case MQTT_EVENT_DISCONNECTED:
            APP_LOGW(TAG, "Disconnected from MQTT broker");
            s_state = MQTT_STATE_DISCONNECTED;
            break;

        case MQTT_EVENT_SUBSCRIBED:
            APP_LOGI(TAG, "Subscribe acknowledged, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            APP_LOGI(TAG, "Unsubscribe acknowledged, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            APP_LOGI(TAG, "Publish acknowledged, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
        {
            // Push message to queue for app_task to process
            if (s_msg_queue == NULL) break;

            mqtt_msg_t msg;
            msg.topic_len = event->topic_len;
            msg.payload_len = event->data_len;

            msg.topic = strndup(event->topic, event->topic_len);
            msg.payload = strndup(event->data, event->data_len);

            if (msg.topic == NULL || msg.payload == NULL) {
                APP_LOGE(TAG, "Failed to allocate MQTT message");
                free(msg.topic);
                free(msg.payload);
                break;
            }

            if (xQueueSend(s_msg_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
                APP_LOGW(TAG, "MQTT message queue full, dropping message");
                free(msg.topic);
                free(msg.payload);
            } else {
                APP_LOGI(TAG, "Queued message from topic '%s' (%d bytes)", msg.topic, msg.payload_len);
            }
            break;
        }

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

// =========================================================================
// Connection Management
// =========================================================================

esp_err_t mqtt_init_and_start(void)
{
    APP_LOGI(TAG, "Initializing MQTT client...");

    // Create message queue
    if (s_msg_queue == NULL) {
        s_msg_queue = xQueueCreate(MQTT_MSG_QUEUE_SIZE, sizeof(mqtt_msg_t));
        if (s_msg_queue == NULL) {
            APP_LOGE(TAG, "Failed to create MQTT message queue");
            return ESP_ERR_NO_MEM;
        }
        APP_LOGI(TAG, "MQTT message queue created (size: %d)", MQTT_MSG_QUEUE_SIZE);
    }

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
    if (s_client == NULL) {
        APP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }

    esp_err_t ret = esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                                    mqtt_event_handler, NULL);
    if (ret != ESP_OK) {
        APP_LOGE(TAG, "Failed to register MQTT event handler");
        return ret;
    }

    ret = esp_mqtt_client_start(s_client);
    if (ret != ESP_OK) {
        APP_LOGE(TAG, "Failed to start MQTT client");
        return ret;
    }

    return ESP_OK;
}

esp_err_t mqtt_stop(void)
{
    if (s_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    APP_LOGI(TAG, "Stopping MQTT client...");
    esp_err_t ret = esp_mqtt_client_stop(s_client);
    if (ret != ESP_OK) {
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
    if (s_client == NULL || !mqtt_is_connected()) {
        APP_LOGE(TAG, "MQTT client is not connected");
        return -1;
    }

    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, len, qos, 0);
    if (msg_id < 0) {
        APP_LOGE(TAG, "Failed to publish to topic %s", topic);
    } else {
        APP_LOGI(TAG, "Published to topic %s, msg_id=%d", topic, msg_id);
    }
    return msg_id;
}

QueueHandle_t mqtt_get_message_queue(void)
{
    return s_msg_queue;
}
