/**
 * @file gpio_control.h
 * @brief Comprehensive GPIO control utility for ESP32
 * 
 * Supports:
 * - Digital I/O (input with pull-up/pull-down, output, open-drain)
 * - Analog input (ADC1/ADC2 with calibration)
 * - PWM output (servo, LED dimming, motor control)
 * - Interrupt-driven input (edge/level triggers)
 * - Servo control (angle-based positioning)
 */
#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "hal/adc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// =========================================================================
// GPIO Mode Types
// =========================================================================

/**
 * @brief GPIO pin mode configuration (application-level)
 */
typedef enum {
    GPIO_CTRL_MODE_DIGITAL_OUTPUT,  /**< Digital output (push-pull) */
    GPIO_CTRL_MODE_DIGITAL_INPUT,   /**< Digital input */
    GPIO_CTRL_MODE_ANALOG_INPUT,    /**< Analog input (ADC) */
    GPIO_CTRL_MODE_PWM_OUTPUT,      /**< PWM output (LEDC) */
    GPIO_CTRL_MODE_SERVO_OUTPUT,    /**< Servo motor output (20ms period) */
    GPIO_CTRL_MODE_OPEN_DRAIN,      /**< Open-drain output (for I2C, etc.) */
} gpio_ctrl_mode_t;

/**
 * @brief Digital pin state
 */
typedef enum {
    PIN_LOW = 0,
    PIN_HIGH = 1
} pin_state_t;

/**
 * @brief Pull-up/pull-down configuration
 */
typedef enum {
    GPIO_PULL_NONE = 0,
    GPIO_PULL_UP,
    GPIO_PULL_DOWN,
    GPIO_PULL_BOTH,
} gpio_pull_t;

/**
 * @brief Interrupt trigger type (application-level)
 */
typedef enum {
    GPIO_CTRL_INTR_DISABLE = 0,
    GPIO_CTRL_INTR_RISING_EDGE,
    GPIO_CTRL_INTR_FALLING_EDGE,
    GPIO_CTRL_INTR_BOTH_EDGES,
    GPIO_CTRL_INTR_LOW_LEVEL,
    GPIO_CTRL_INTR_HIGH_LEVEL,
} gpio_ctrl_intr_type_t;

/**
 * @brief ADC resolution options
 */
typedef enum {
    ADC_RESOLUTION_9_BIT = 9,   /**< 9-bit (0-511) */
    ADC_RESOLUTION_10_BIT = 10, /**< 10-bit (0-1023) */
    ADC_RESOLUTION_11_BIT = 11, /**< 11-bit (0-2047) */
    ADC_RESOLUTION_12_BIT = 12, /**< 12-bit (0-4095) */
} adc_resolution_t;

// =========================================================================
// PWM Configuration
// =========================================================================

/**
 * @brief PWM configuration structure
 */
typedef struct {
    uint32_t frequency_hz;      /**< PWM frequency in Hz */
    uint8_t resolution_bits;    /**< LEDC resolution (1-15 bits) */
    uint8_t duty_cycle_percent; /**< Initial duty cycle (0-100) */
    ledc_channel_t channel;     /**< LEDC channel (LEDC_CHANNEL_0-7) */
    ledc_timer_t timer;         /**< LEDC timer (LEDC_TIMER_0-3) */
} pwm_config_t;

/**
 * @brief Servo configuration structure
 */
typedef struct {
    uint16_t min_pulse_us;      /**< Minimum pulse width (typically 500-1000 us) */
    uint16_t max_pulse_us;      /**< Maximum pulse width (typically 2000-2500 us) */
    uint16_t min_angle;         /**< Minimum angle (typically 0) */
    uint16_t max_angle;         /**< Maximum angle (typically 180) */
    ledc_channel_t channel;     /**< LEDC channel */
    ledc_timer_t timer;         /**< LEDC timer */
} servo_config_t;

// =========================================================================
// ADC Configuration
// =========================================================================

/**
 * @brief ADC configuration structure
 */
typedef struct {
    adc_unit_t unit;            /**< ADC unit (ADC_UNIT_1 or ADC_UNIT_2) */
    adc_channel_t channel;      /**< ADC channel */
    adc_resolution_t resolution;/**< ADC resolution */
    adc_atten_t attenuation;    /**< Input attenuation (0-11dB) */
} adc_config_t;

// =========================================================================
// Interrupt Callback
// =========================================================================

/**
 * @brief GPIO interrupt callback function type
 * @param pin GPIO pin number that triggered the interrupt
 * @param arg User argument passed during registration
 */
typedef void (*gpio_isr_callback_t)(void *arg);

// =========================================================================
// Initialization Functions
// =========================================================================

/**
 * @brief Initialize GPIO control system
 * Must be called before using any other GPIO functions
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t gpio_control_init(void);

// =========================================================================
// Digital I/O Functions
// =========================================================================

/**
 * @brief Configure a pin for digital output
 * @param pin GPIO pin number
 * @param initial_state Initial output state (PIN_LOW or PIN_HIGH)
 * @param pull Pull-up/pull-down configuration
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t gpio_digital_output_init(int pin, pin_state_t initial_state, gpio_pull_t pull);

/**
 * @brief Configure a pin for digital input
 * @param pin GPIO pin number
 * @param pull Pull-up/pull-down configuration
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t gpio_digital_input_init(int pin, gpio_pull_t pull);

/**
 * @brief Set digital output pin state
 * @param pin GPIO pin number (must be configured as output)
 * @param state PIN_HIGH or PIN_LOW
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t gpio_digital_write(int pin, pin_state_t state);

/**
 * @brief Read digital input pin state
 * @param pin GPIO pin number (must be configured as input)
 * @return PIN_HIGH, PIN_LOW, or -1 on error
 */
int gpio_digital_read(int pin);

/**
 * @brief Toggle digital output pin state
 * @param pin GPIO pin number (must be configured as output)
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t gpio_digital_toggle(int pin);

// =========================================================================
// Analog Input (ADC) Functions
// =========================================================================

/**
 * @brief Configure a pin for analog input (ADC)
 * @param config ADC configuration structure
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t gpio_analog_init(const adc_config_t *config);

/**
 * @brief Read raw ADC value from a pin
 * @param pin GPIO pin number (must be ADC-capable)
 * @return Raw ADC value (0-4095 for 12-bit), or -1 on error
 */
int gpio_analog_read_raw(int pin);

/**
 * @brief Read ADC value converted to millivolts
 * @param pin GPIO pin number (must be ADC-capable)
 * @return Voltage in millivolts, or -1 on error
 */
int gpio_analog_read_millivolts(int pin);

/**
 * @brief Read ADC percentage (0-100%) based on reference voltage
 * @param pin GPIO pin number
 * @param max_mv Maximum voltage for 100% (typically 3300 for 3.3V)
 * @return Percentage (0-100), or -1 on error
 */
int gpio_analog_read_percent(int pin, int max_mv);

// =========================================================================
// PWM Output Functions
// =========================================================================

/**
 * @brief Configure a pin for PWM output
 * @param pin GPIO pin number
 * @param config PWM configuration structure
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t gpio_pwm_init(int pin, const pwm_config_t *config);

/**
 * @brief Set PWM duty cycle
 * @param pin GPIO pin number (must be configured as PWM)
 * @param duty_percent Duty cycle percentage (0-100)
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t gpio_pwm_set_duty(int pin, uint8_t duty_percent);

/**
 * @brief Set PWM frequency
 * @param pin GPIO pin number (must be configured as PWM)
 * @param frequency_hz New frequency in Hz
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t gpio_pwm_set_frequency(int pin, uint32_t frequency_hz);

/**
 * @brief Start PWM output
 * @param pin GPIO pin number
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t gpio_pwm_start(int pin);

/**
 * @brief Stop PWM output
 * @param pin GPIO pin number
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t gpio_pwm_stop(int pin);

// =========================================================================
// Servo Control Functions
// =========================================================================

/**
 * @brief Configure a pin for servo control
 * @param pin GPIO pin number
 * @param config Servo configuration structure
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t gpio_servo_init(int pin, const servo_config_t *config);

/**
 * @brief Set servo angle
 * @param pin GPIO pin number (must be configured as servo)
 * @param angle Desired angle (within configured min/max range)
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t gpio_servo_set_angle(int pin, uint16_t angle);

/**
 * @brief Set servo pulse width directly
 * @param pin GPIO pin number (must be configured as servo)
 * @param pulse_us Pulse width in microseconds
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t gpio_servo_set_pulse(int pin, uint16_t pulse_us);

// =========================================================================
// Interrupt Functions
// =========================================================================

/**
 * @brief Configure a pin for interrupt-driven input
 * @param pin GPIO pin number
 * @param pull Pull-up/pull-down configuration
 * @param trigger Interrupt trigger type
 * @param callback Callback function to call on interrupt
 * @param arg User argument to pass to callback
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t gpio_interrupt_init(int pin, gpio_pull_t pull, gpio_ctrl_intr_type_t trigger,
                              gpio_isr_callback_t callback, void *arg);

/**
 * @brief Install GPIO ISR service (call once before using interrupts)
 * @param max_handlers Maximum number of interrupt handlers
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t gpio_isr_service_install(int max_handlers);

/**
 * @brief Uninstall GPIO ISR service
 * @return ESP_OK on success, or an error code on failure
 */
esp_err_t gpio_isr_service_uninstall(void);

// =========================================================================
// Utility Functions
// =========================================================================

/**
 * @brief Check if a GPIO pin supports ADC
 * @param pin GPIO pin number
 * @return true if pin supports ADC, false otherwise
 */
bool gpio_is_adc_capable(int pin);

/**
 * @brief Check if a GPIO pin supports PWM (LEDC)
 * @param pin GPIO pin number
 * @return true if pin supports PWM, false otherwise
 */
bool gpio_is_pwm_capable(int pin);

/**
 * @brief Get the ADC unit for a given pin
 * @param pin GPIO pin number
 * @return ADC unit (ADC_UNIT_1 or ADC_UNIT_2), or -1 if not ADC-capable
 */
int gpio_get_adc_unit(int pin);

/**
 * @brief Get the ADC channel for a given pin
 * @param pin GPIO pin number
 * @return ADC channel, or -1 if not ADC-capable
 */
int gpio_get_adc_channel(int pin);

/**
 * @brief Check if an ADC channel is already configured
 * @param unit ADC unit (ADC_UNIT_1 or ADC_UNIT_2)
 * @param channel ADC channel
 * @return true if channel is configured, false otherwise
 */
bool gpio_adc_channel_is_configured(adc_unit_t unit, adc_channel_t channel);

#ifdef __cplusplus
}
#endif



