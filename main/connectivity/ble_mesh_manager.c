// SPDX-FileCopyrightText: 2021-2026 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0

#include "ble_mesh_manager.h"
#include "esp_log.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>
#include "app_logging.h"

#define TAG "BLE_MESH"
#define MESH_RX_QUEUE_LEN 15
#define MESH_MAX_DATA_LEN  128  // reasonable default payload size

/* Internal packet representation */
typedef struct {
    uint16_t src_addr;
    uint8_t  data[MESH_MAX_DATA_LEN];
    size_t   len;
} mesh_packet_t;

static QueueHandle_t    s_rx_queue = NULL;
static mesh_data_cb_t   s_user_cb  = NULL;
static esp_ble_mesh_model_t *s_send_model = NULL;

/* ------------------------------------------------------------------------- */
static void _invoke_user_cb(const mesh_packet_t *pkt)
{
    if (s_user_cb) {
        s_user_cb(pkt->src_addr, pkt->data, pkt->len);
    }
}

/* ------------------------------------------------------------------------- */
static esp_err_t _enqueue_packet(uint16_t src, const uint8_t *data, uint16_t len)
{
    if (!s_rx_queue) {
        APP_LOGW(TAG, "RX queue not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (len > MESH_MAX_DATA_LEN) {
        APP_LOGW(TAG, "Payload too large (%u > %d)", len, MESH_MAX_DATA_LEN);
        return ESP_ERR_INVALID_SIZE;
    }
    mesh_packet_t *pkt = (mesh_packet_t *)malloc(sizeof(mesh_packet_t));
    if (!pkt) {
        APP_LOGE(TAG, "Failed to allocate memory for mesh packet");
        return ESP_ERR_NO_MEM;
    }
    pkt->src_addr = src;
    pkt->len = len;
    memcpy(pkt->data, data, len);
    if (xQueueSend(s_rx_queue, &pkt, 0) != pdTRUE) {
        APP_LOGW(TAG, "RX queue full, dropping packet");
        free(pkt);
        return ESP_ERR_NO_MEM;
    }
    _invoke_user_cb(pkt);
    return ESP_OK;
}

/* ------------------------------------------------------------------------- */
static void _model_op_cb(esp_ble_mesh_model_cb_event_t event,
                         esp_ble_mesh_model_cb_param_t *param)
{
    if (event != ESP_BLE_MESH_MODEL_OPERATION_EVT) {
        return;
    }
    if (!param || !param->model_operation.model || !param->model_operation.ctx) {
        return;
    }
    const uint8_t *payload = param->model_operation.msg;
    uint16_t payload_len = param->model_operation.length;
    uint16_t src_addr = param->model_operation.ctx->addr;
    _enqueue_packet(src_addr, payload, payload_len);
}

/* ------------------------------------------------------------------------- */
/* Provisioning callback – handles provisioning lifecycle events */
static void _prov_cb(esp_ble_mesh_prov_cb_event_t event,
                     esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        APP_LOGI(TAG, "Provisioning complete, addr: 0x%04x",
                 param->node_prov_complete.addr);
        break;
    case ESP_BLE_MESH_PROVISIONER_PROV_COMPLETE_EVT:
        APP_LOGI(TAG, "Provisioner: node provisioned, addr: 0x%04x",
                 param->provisioner_prov_complete.unicast_addr);
        break;
    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        APP_LOGW(TAG, "Node provisioning reset");
        break;
    default:
        APP_LOGD(TAG, "Prov event %d", event);
        break;
    }
}

/* ------------------------------------------------------------------------- */
// BLE Mesh composition structures

/* Vendor model OUI/company: use 0xFFFF (test) for development */
#define VENDOR_COMPANY_ID   0xFFFF
#define VENDOR_MODEL_ID     0x0001

/* Vendor model opcodes — must be 3-byte (company_id << 16 | opcode) in the
   model op table, per BLE Mesh spec. The 1-byte raw values are used in
   mesh_send() for the wire format. */
#define VENDOR_OPCODE_SET   0xC1
#define VENDOR_OPCODE_GET   0xC2
#define VENDOR_OPCODE_STATUS 0xC3

/* Full 3-byte opcodes for the model op table */
#define VENDOR_OPCODE_SET_FULL   ((VENDOR_COMPANY_ID << 16) | VENDOR_OPCODE_SET)
#define VENDOR_OPCODE_GET_FULL   ((VENDOR_COMPANY_ID << 16) | VENDOR_OPCODE_GET)

static esp_ble_mesh_client_t config_client;

static esp_ble_mesh_cfg_srv_t config_server = {
    .relay = ESP_BLE_MESH_RELAY_ENABLED,
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER)
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
#endif
    .default_ttl = 7,
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(3, 20),
};

/* Vendor-specific models — opcodes MUST be 3-byte in the op table */
static esp_ble_mesh_model_op_t vendor_op[] = {
    ESP_BLE_MESH_MODEL_OP(VENDOR_OPCODE_SET_FULL, 2),
    ESP_BLE_MESH_MODEL_OP(VENDOR_OPCODE_GET_FULL, 1),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_t vendor_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(VENDOR_COMPANY_ID, VENDOR_MODEL_ID, vendor_op, NULL, NULL),
};

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_CFG_CLI(&config_client),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, vendor_models),
};

static esp_ble_mesh_comp_t composition = {
    .cid = VENDOR_COMPANY_ID,  // Use 0xFFFF for development/testing
    .elements = elements,
    .element_count = sizeof(elements) / sizeof(elements[0]),
};

static const uint8_t dev_uuid[16] = { 0x32, 0x10 }; // default unprovisioned device UUID
static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
};

esp_err_t mesh_init(void)
{
    APP_LOGI(TAG, "Initializing BLE Mesh utils");
    if (s_rx_queue) {
        APP_LOGI(TAG, "BLE Mesh already initialized");
        return ESP_OK; // already init
    }

    esp_err_t err;

    // 1. Initialize BT Controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    err = esp_bt_controller_init(&bt_cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        APP_LOGE(TAG, "Bluetooth controller init failed: %s", esp_err_to_name(err));
        return err;
    } else {
        APP_LOGI(TAG, "Bluetooth controller initialized");
    }

    err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        APP_LOGE(TAG, "Bluetooth controller enable failed: %s", esp_err_to_name(err));
        return err;
    } else {
        APP_LOGI(TAG, "Bluetooth controller enabled");
    }

    // 2. Initialize Bluedroid host
    err = esp_bluedroid_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        APP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(err));
        return err;
    } else {
        APP_LOGI(TAG, "Bluedroid initialized");
    }

    err = esp_bluedroid_enable();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        APP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(err));
        return err;
    } else {
        APP_LOGI(TAG, "Bluedroid enabled");
    }

    // 3. Initialize BLE Mesh stack
    err = esp_ble_mesh_init(&provision, &composition);
    if (err != ESP_OK) {
        APP_LOGE(TAG, "esp_ble_mesh_init failed: %s", esp_err_to_name(err));
        return err;
    } else {
        APP_LOGI(TAG, "BLE Mesh stack initialized");
    }

    // 4. Register provisioning callback
    err = esp_ble_mesh_register_prov_callback(_prov_cb);
    if (err != ESP_OK) {
        APP_LOGE(TAG, "Failed to register prov callback: %s", esp_err_to_name(err));
        return err;
    } else {
        APP_LOGI(TAG, "Provisioning callback registered");
    }

    s_rx_queue = xQueueCreate(MESH_RX_QUEUE_LEN, sizeof(mesh_packet_t *));
    if (!s_rx_queue) {
        APP_LOGE(TAG, "Failed to create RX queue");
        return ESP_ERR_NO_MEM;
    } else {
        APP_LOGI(TAG, "RX queue created (depth %d)", MESH_RX_QUEUE_LEN);
    }

    /* Register a generic model callback that will be invoked for all vendor models. */
    err = esp_ble_mesh_register_custom_model_callback(_model_op_cb);
    if (err != ESP_OK) {
        APP_LOGE(TAG, "Failed to register custom model callback: %s", esp_err_to_name(err));
        vQueueDelete(s_rx_queue);
        s_rx_queue = NULL;
        return err;
    } else {
        APP_LOGI(TAG, "Custom model callback registered");
    }

    APP_LOGI(TAG, "BLE Mesh utils initialized (queue depth %d)", MESH_RX_QUEUE_LEN);
    return ESP_OK;
}

esp_err_t mesh_start_provisioner(bool primary_role)
{
    esp_err_t err;

    if (primary_role) {
        /* Provisioner role: this device provisions other nodes */
        err = esp_ble_mesh_provisioner_prov_enable(
                  ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
    } else {
        /* Node role: this device accepts provisioning */
        err = esp_ble_mesh_node_prov_enable(
                  ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
    }

    if (err != ESP_OK) {
        APP_LOGE(TAG, "Failed to enable %s: %s",
                 primary_role ? "provisioner" : "node",
                 esp_err_to_name(err));
        return err;
    }
    APP_LOGI(TAG, "Provisioning started (%s)",
             primary_role ? "provisioner" : "node");
    return ESP_OK;
}

esp_err_t mesh_set_send_model(void *model)
{
    if (!model) {
        return ESP_ERR_INVALID_ARG;
    }
    s_send_model = (esp_ble_mesh_model_t *)model;
    APP_LOGI(TAG, "Send model set (addr %p)", s_send_model);
    return ESP_OK;
}

esp_err_t mesh_send(uint16_t dst, const uint8_t *data, size_t len)
{
    if (!s_send_model) {
        APP_LOGE(TAG, "Send model not set");
        return ESP_ERR_INVALID_STATE;
    }
    if (len > UINT16_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_ble_mesh_msg_ctx_t ctx = {
        .net_idx = 0,
        .app_idx = 0,
        .addr = dst,
        .send_ttl = 3,
        .send_rel = false,
    };
    /* Vendor opcode: 3-byte opcode = company_id << 16 | opcode */
    uint32_t opcode = ((uint32_t)VENDOR_COMPANY_ID << 16) | VENDOR_OPCODE_SET;
    esp_err_t err = esp_ble_mesh_client_model_send_msg(
        s_send_model,
        &ctx,
        opcode,
        len,
        (uint8_t *)data,
        0,   // msg_timeout
        false, // need_rsp
        ROLE_NODE
    );
    if (err != ESP_OK) {
        APP_LOGE(TAG, "mesh_send failed: %s", esp_err_to_name(err));
    } else {
        APP_LOGI(TAG, "Sent %zu bytes to 0x%04x", len, dst);
    }
    return err;
}

esp_err_t mesh_forward(uint16_t src_addr, const uint8_t *data, size_t len, uint16_t next_hop)
{
    // Logging the forward action
    APP_LOGI(TAG, "Forwarding %zu bytes from 0x%04x to 0x%04x", len, src_addr, next_hop);
    // For now, forwarding just re‑uses mesh_send
    return mesh_send(next_hop, data, len);
}

esp_err_t mesh_register_data_cb(mesh_data_cb_t cb)
{
    s_user_cb = cb;
    APP_LOGI(TAG, "User data callback %sregistered", cb ? "" : "de");
    return ESP_OK;
}

void mesh_deinit(void)
{
    if (s_rx_queue) {
        // Drain and free any pending packets
        mesh_packet_t *pkt = NULL;
        while (xQueueReceive(s_rx_queue, &pkt, 0) == pdTRUE) {
            free(pkt);
        }
        vQueueDelete(s_rx_queue);
        s_rx_queue = NULL;
    }
    s_user_cb = NULL;
    s_send_model = NULL;
    APP_LOGI(TAG, "BLE Mesh utils de‑initialized");
}

/* ------------------------------------------------------------------------- */
// Helper to retrieve a packet from the queue (optional for user code)
esp_err_t mesh_rx_get(uint16_t *src, uint8_t *data, size_t *len)
{
    if (!s_rx_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    mesh_packet_t *pkt = NULL;
    if (xQueueReceive(s_rx_queue, &pkt, 0) != pdTRUE) {
        return ESP_ERR_NOT_FOUND;
    }
    if (src) *src = pkt->src_addr;
    if (data && len && *len >= pkt->len) {
        memcpy(data, pkt->data, pkt->len);
        *len = pkt->len;
    } else if (len) {
        *len = pkt->len;
    }
    free(pkt);
    return ESP_OK;
}
