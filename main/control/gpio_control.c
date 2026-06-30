/**
 * @file gpio_control.c
 * @brief This file contains the GPIO control functions for the application.
 */
#include "gpio_control.h"
#include "driver/gpio.h"

esp_err_t gpio_set_pin(int pin, pin_state_t state) 
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        return ret;
    }
    return gpio_set_level(pin, state);
}

int gpio_get_pin(int pin) 
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        return -1; // Indicate error
    }
    return (gpio_get_level(pin) == 0) ? GPIO_PIN_LOW : GPIO_PIN_HIGH;
}