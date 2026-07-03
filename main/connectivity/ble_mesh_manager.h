#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

/* Callback type for inbound mesh packets */
typedef void (*mesh_data_cb_t)(uint16_t src_addr, const uint8_t *data, size_t len);

/**
 * @brief Initialise BLE‑Mesh subsystem and register internal callbacks.
 *
 * This function registers provisioning and vendor‑model callbacks, creates an
 * internal FreeRTOS queue (depth 15) for inbound packets and prepares the BLE‑
 * Mesh stack for operation.
 *
 * @return ESP_OK on success, otherwise an esp_err_t code.
 */
esp_err_t mesh_init(void);

/**
 * @brief Start the provisioner role.
 *
 * @param primary_role  True if this device should act as the primary provisioner.
 * @return ESP_OK on success.
 */
esp_err_t mesh_start_provisioner(bool primary_role);

/**
 * @brief Send a raw payload to a destination address using a previously set model.
 *
 * @param dst   Destination unicast address.
 * @param data  Pointer to payload data.
 * @param len   Length of payload.
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if a model has not been set.
 */
esp_err_t mesh_send(uint16_t dst, const uint8_t *data, size_t len);

/**
 * @brief Forward a received packet to another node.
 *
 * This is essentially a thin wrapper around mesh_send that also records the
 * source address for logging.
 *
 * @param src_addr  Source address of the packet being forwarded.
 * @param data      Payload to forward.
 * @param len       Length of payload.
 * @param next_hop  Destination address for the forward.
 * @return ESP_OK on success.
 */
esp_err_t mesh_forward(uint16_t src_addr, const uint8_t *data, size_t len, uint16_t next_hop);

/**
 * @brief Register a user‑provided callback that will be invoked for each inbound packet.
 *
 * The callback is called from the internal BLE‑Mesh event context, so it should be
 * short and non‑blocking.
 *
 * @param cb  Callback function pointer. Pass NULL to deregister.
 * @return ESP_OK on success.
 */
esp_err_t mesh_register_data_cb(mesh_data_cb_t cb);

/**
 * @brief Provide the model instance that will be used for sending data.
 *
 * The user must call this after the BLE‑Mesh models have been created (usually in
 * the provisioning callback).
 *
 * @param model Pointer to an esp_ble_mesh_model_t that supports vendor messages.
 * @return ESP_OK on success.
 */
esp_err_t mesh_set_send_model(void *model);

/**
 * @brief De‑initialise the BLE‑Mesh utilities and release resources.
 */
void mesh_deinit(void);


