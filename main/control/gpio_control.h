/**
 * @file gpio_control.h
 * @brief This file contains the GPIO control functions for the application.
 */
#pragma once

#include "esp_err.h"

typedef enum {
    GPIO_PIN_LOW = 0,  /**< GPIO pin is low (0) */
    GPIO_PIN_HIGH = 1  /**< GPIO pin is high (1) */
} pin_state_t;

/**
 * @brief Set the level of a GPIO pin
 * @param pin The GPIO pin number
 * @param level The level to set (GPIO_PIN_LOW for low, GPIO_PIN_HIGH for high)
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t gpio_set_pin(int pin, pin_state_t state);

/**
 * @brief Get the level of a GPIO pin
 * @param pin The GPIO pin number
 * @return The level of the pin (GPIO_PIN_LOW for low, GPIO_PIN_HIGH for high), or -1 on error
 */
int gpio_get_pin(int pin);



