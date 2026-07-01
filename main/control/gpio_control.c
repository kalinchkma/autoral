/**
 * @file gpio_control.c
 * @brief Comprehensive GPIO control utility for ESP32
 * 
 * Supports:
 * - Digital I/O (input with pull-up/pull-down, output, open-drain)
 * - Analog input (ADC1/ADC2 with calibration)
 * - PWM output (servo, LED dimming, motor control)
 * - Interrupt-driven input (edge/level triggers)
 * - Servo control (angle-based positioning)
 */
#include "gpio_control.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "GPIO_CTRL";

// =========================================================================
// Pin Tracking Arrays
// =========================================================================

#define MAX_GPIO_PINS 40

typedef struct {
    gpio_ctrl_mode_t mode;
    bool configured;
    union {
        struct {
            bool is_output;
            pin_state_t state;
        } digital;
        struct {
            adc_unit_t unit;
            adc_channel_t channel;
            adc_resolution_t resolution;
            adc_atten_t attenuation;
        } analog;
        struct {
            ledc_channel_t channel;
            ledc_timer_t timer;
            uint32_t frequency;
            uint8_t resolution;
        } pwm;
        struct {
            ledc_channel_t channel;
            ledc_timer_t timer;
            uint16_t min_pulse;
            uint16_t max_pulse;
            uint16_t min_angle;
            uint16_t max_angle;
        } servo;
        struct {
            gpio_isr_callback_t callback;
            void *arg;
            gpio_ctrl_intr_type_t trigger;
        } interrupt;
    };
} pin_config_t;

static pin_config_t s_pins[MAX_GPIO_PINS];
static bool s_initialized = false;
static bool s_isr_installed = false;

// ADC handles for oneshot mode
static adc_oneshot_unit_handle_t s_adc1_handle = NULL;
static adc_oneshot_unit_handle_t s_adc2_handle = NULL;
static adc_cali_handle_t s_adc1_cali_handle = NULL;
static adc_cali_handle_t s_adc2_cali_handle = NULL;

// Track which ADC channels are configured per unit
static bool s_adc1_channels_configured[10] = {false};  // ADC1 has up to 10 channels
static bool s_adc2_channels_configured[10] = {false};  // ADC2 has up to 10 channels

// =========================================================================
// ADC Pin Mapping (ESP32)
// =========================================================================

// ADC1 channels: GPIOs 32-39 (ESP-IDF v6.x channel mapping)
static const struct {
    int gpio;
    adc_channel_t channel;
} adc1_pins[] = {
    {36, ADC_CHANNEL_0},  // VP
    {39, ADC_CHANNEL_3},  // VN
    {34, ADC_CHANNEL_6},
    {35, ADC_CHANNEL_7},
    {32, ADC_CHANNEL_4},
    {33, ADC_CHANNEL_5},
    {25, ADC_CHANNEL_8},
    {26, ADC_CHANNEL_9},
    {27, ADC_CHANNEL_9},  // Shares channel with GPIO26 on ESP32
    {14, ADC_CHANNEL_6},  // Shares channel with GPIO34 on ESP32
    {-1, 0}  // Terminator
};

// ADC2 channels: GPIOs 0, 2, 4, 15-27 (note: ADC2 conflicts with WiFi)
static const struct {
    int gpio;
    adc_channel_t channel;
} adc2_pins[] = {
    {0, ADC_CHANNEL_0},
    {2, ADC_CHANNEL_2},
    {4, ADC_CHANNEL_4},
    {15, ADC_CHANNEL_5},
    {25, ADC_CHANNEL_8},  // Also on ADC1
    {26, ADC_CHANNEL_9},  // Also on ADC1
    {27, ADC_CHANNEL_9},  // Also on ADC1 (shares with GPIO26)
    {-1, 0}  // Terminator
};

// =========================================================================
// Internal Helper Functions
// =========================================================================

static bool is_valid_pin(int pin) {
    return (pin >= 0 && pin < MAX_GPIO_PINS);
}

static bool is_valid_adc1_pin(int pin) {
    for (int i = 0; adc1_pins[i].gpio != -1; i++) {
        if (adc1_pins[i].gpio == pin) return true;
    }
    return false;
}

static bool is_valid_adc2_pin(int pin) {
    for (int i = 0; adc2_pins[i].gpio != -1; i++) {
        if (adc2_pins[i].gpio == pin) return true;
    }
    return false;
}

static adc_channel_t get_adc_channel(int pin, adc_unit_t unit) {
    if (unit == ADC_UNIT_1) {
        for (int i = 0; adc1_pins[i].gpio != -1; i++) {
            if (adc1_pins[i].gpio == pin) return adc1_pins[i].channel;
        }
    } else if (unit == ADC_UNIT_2) {
        for (int i = 0; adc2_pins[i].gpio != -1; i++) {
            if (adc2_pins[i].gpio == pin) return adc2_pins[i].channel;
        }
    }
    return -1;
}

static gpio_num_t pin_to_gpio(int pin) {
    return (gpio_num_t)pin;
}

// =========================================================================
// Initialization
// =========================================================================

esp_err_t gpio_control_init(void) {
    if (s_initialized) {
        ESP_LOGW(TAG, "GPIO control already initialized");
        return ESP_OK;
    }
    
    memset(s_pins, 0, sizeof(s_pins));
    s_initialized = true;
    
    ESP_LOGI(TAG, "GPIO control system initialized");
    return ESP_OK;
}

// =========================================================================
// Digital I/O Functions
// =========================================================================

esp_err_t gpio_digital_output_init(int pin, pin_state_t initial_state, gpio_pull_t pull) {
    if (!is_valid_pin(pin)) {
        ESP_LOGE(TAG, "Invalid pin number: %d", pin);
        return ESP_ERR_INVALID_ARG;
    }
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = (pull == GPIO_PULL_UP || pull == GPIO_PULL_BOTH) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = (pull == GPIO_PULL_DOWN || pull == GPIO_PULL_BOTH) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure pin %d as output: %s", pin, esp_err_to_name(ret));
        return ret;
    }
    
    // Set initial state
    ret = gpio_set_level(pin, initial_state);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set initial state for pin %d: %s", pin, esp_err_to_name(ret));
        return ret;
    }
    
    s_pins[pin].mode = GPIO_CTRL_MODE_DIGITAL_OUTPUT;
    s_pins[pin].configured = true;
    s_pins[pin].digital.is_output = true;
    s_pins[pin].digital.state = initial_state;
    
    ESP_LOGI(TAG, "Pin %d configured as digital output, initial state: %d", pin, initial_state);
    return ESP_OK;
}

esp_err_t gpio_digital_input_init(int pin, gpio_pull_t pull) {
    if (!is_valid_pin(pin)) {
        ESP_LOGE(TAG, "Invalid pin number: %d", pin);
        return ESP_ERR_INVALID_ARG;
    }
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = (pull == GPIO_PULL_UP || pull == GPIO_PULL_BOTH) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = (pull == GPIO_PULL_DOWN || pull == GPIO_PULL_BOTH) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure pin %d as input: %s", pin, esp_err_to_name(ret));
        return ret;
    }
    
    s_pins[pin].mode = GPIO_CTRL_MODE_DIGITAL_INPUT;
    s_pins[pin].configured = true;
    s_pins[pin].digital.is_output = false;
    
    ESP_LOGI(TAG, "Pin %d configured as digital input", pin);
    return ESP_OK;
}

esp_err_t gpio_digital_write(int pin, pin_state_t state) {
    if (!is_valid_pin(pin) || !s_pins[pin].configured) {
        ESP_LOGE(TAG, "Pin %d not configured or invalid", pin);
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_pins[pin].mode != GPIO_CTRL_MODE_DIGITAL_OUTPUT) {
        ESP_LOGE(TAG, "Pin %d is not configured as digital output", pin);
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = gpio_set_level(pin, state);
    if (ret == ESP_OK) {
        s_pins[pin].digital.state = state;
    }
    return ret;
}

int gpio_digital_read(int pin) {
    if (!is_valid_pin(pin) || !s_pins[pin].configured) {
        ESP_LOGE(TAG, "Pin %d not configured or invalid", pin);
        return -1;
    }
    
    if (s_pins[pin].mode != GPIO_CTRL_MODE_DIGITAL_INPUT) {
        ESP_LOGE(TAG, "Pin %d is not configured as digital input", pin);
        return -1;
    }
    
    return gpio_get_level(pin);
}

esp_err_t gpio_digital_toggle(int pin) {
    if (!is_valid_pin(pin) || !s_pins[pin].configured) {
        ESP_LOGE(TAG, "Pin %d not configured or invalid", pin);
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_pins[pin].mode != GPIO_CTRL_MODE_DIGITAL_OUTPUT) {
        ESP_LOGE(TAG, "Pin %d is not configured as digital output", pin);
        return ESP_ERR_INVALID_STATE;
    }
    
    pin_state_t new_state = (s_pins[pin].digital.state == PIN_HIGH) ? PIN_LOW : PIN_HIGH;
    return gpio_digital_write(pin, new_state);
}

// =========================================================================
// Analog Input (ADC) Functions
// =========================================================================

static esp_err_t adc_oneshot_init_unit(adc_unit_t unit, adc_oneshot_unit_handle_t *handle) {
    if (*handle != NULL) {
        return ESP_OK;  // Already initialized
    }
    
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    
    esp_err_t ret = adc_oneshot_new_unit(&init_cfg, handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init ADC%d oneshot unit: %s", unit + 1, esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "ADC%d oneshot unit initialized", unit + 1);
    return ESP_OK;
}

static esp_err_t adc_cali_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *handle) {
    if (*handle != NULL) {
        return ESP_OK;  // Already initialized
    }
    
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_12,
    };
    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_config, handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ADC curve fitting calibration failed: %s", esp_err_to_name(ret));
        *handle = NULL;
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_12,
    };
    esp_err_t ret = adc_cali_create_scheme_line_fitting(&cali_config, handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ADC line fitting calibration failed: %s", esp_err_to_name(ret));
        *handle = NULL;
    }
#else
    ESP_LOGW(TAG, "ADC calibration not supported on this chip");
    *handle = NULL;
#endif
    
    return ESP_OK;
}

esp_err_t gpio_analog_init(const adc_config_t *config) {
    if (!config) {
        ESP_LOGE(TAG, "ADC config is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Find GPIO pin for this ADC channel
    int pin = -1;
    for (int i = 0; adc1_pins[i].gpio != -1; i++) {
        if (adc1_pins[i].channel == config->channel && config->unit == ADC_UNIT_1) {
            pin = adc1_pins[i].gpio;
            break;
        }
    }
    if (pin == -1) {
        for (int i = 0; adc2_pins[i].gpio != -1; i++) {
            if (adc2_pins[i].channel == config->channel && config->unit == ADC_UNIT_2) {
                pin = adc2_pins[i].gpio;
                break;
            }
        }
    }
    
    if (pin == -1) {
        ESP_LOGE(TAG, "Invalid ADC channel %d for unit %d", config->channel, config->unit);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Initialize ADC unit if needed
    esp_err_t ret;
    if (config->unit == ADC_UNIT_1) {
        ret = adc_oneshot_init_unit(ADC_UNIT_1, &s_adc1_handle);
        if (ret != ESP_OK) return ret;
        ret = adc_cali_init(ADC_UNIT_1, config->attenuation, &s_adc1_cali_handle);
        if (ret != ESP_OK) return ret;
    } else {
        ret = adc_oneshot_init_unit(ADC_UNIT_2, &s_adc2_handle);
        if (ret != ESP_OK) return ret;
        ret = adc_cali_init(ADC_UNIT_2, config->attenuation, &s_adc2_cali_handle);
        if (ret != ESP_OK) return ret;
    }
    
    // Configure channel
    adc_oneshot_unit_handle_t handle = (config->unit == ADC_UNIT_1) ? s_adc1_handle : s_adc2_handle;
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = config->attenuation,
        .bitwidth = (adc_bitwidth_t)config->resolution,
    };
    ret = adc_oneshot_config_channel(handle, config->channel, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_pins[pin].mode = GPIO_CTRL_MODE_ANALOG_INPUT;
    s_pins[pin].configured = true;
    s_pins[pin].analog.unit = config->unit;
    s_pins[pin].analog.channel = config->channel;
    s_pins[pin].analog.resolution = config->resolution;
    s_pins[pin].analog.attenuation = config->attenuation;
    
    ESP_LOGI(TAG, "Pin %d configured as analog input (ADC%d, CH%d)", 
             pin, config->unit + 1, config->channel);
    return ESP_OK;
}

int gpio_analog_read_raw(int pin) {
    if (!is_valid_pin(pin) || !s_pins[pin].configured) {
        ESP_LOGE(TAG, "Pin %d not configured or invalid", pin);
        return -1;
    }
    
    if (s_pins[pin].mode != GPIO_CTRL_MODE_ANALOG_INPUT) {
        ESP_LOGE(TAG, "Pin %d is not configured as analog input", pin);
        return -1;
    }
    
    adc_oneshot_unit_handle_t handle = (s_pins[pin].analog.unit == ADC_UNIT_1) ? s_adc1_handle : s_adc2_handle;
    if (handle == NULL) {
        ESP_LOGE(TAG, "ADC unit not initialized for pin %d", pin);
        return -1;
    }
    
    int raw = 0;
    esp_err_t ret = adc_oneshot_read(handle, s_pins[pin].analog.channel, &raw);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ADC on pin %d: %s", pin, esp_err_to_name(ret));
        return -1;
    }
    
    return raw;
}

int gpio_analog_read_millivolts(int pin) {
    if (!is_valid_pin(pin) || !s_pins[pin].configured) {
        return -1;
    }
    
    if (s_pins[pin].mode != GPIO_CTRL_MODE_ANALOG_INPUT) {
        return -1;
    }
    
    int raw = gpio_analog_read_raw(pin);
    if (raw < 0) return -1;
    
    // Try calibration first
    adc_cali_handle_t cali_handle = (s_pins[pin].analog.unit == ADC_UNIT_1) ? s_adc1_cali_handle : s_adc2_cali_handle;
    
    if (cali_handle != NULL) {
        int voltage = 0;
        esp_err_t ret = adc_cali_raw_to_voltage(cali_handle, raw, &voltage);
        if (ret == ESP_OK) {
            return voltage;  // Already in mV
        }
        ESP_LOGW(TAG, "Calibration failed, using raw conversion");
    }
    
    // Fallback: manual conversion based on attenuation
    int max_mv = 3300;  // 3.3V reference
    
    switch (s_pins[pin].analog.attenuation) {
        case ADC_ATTEN_DB_0:   max_mv = 1100; break;   // 0-1.1V
        case ADC_ATTEN_DB_2_5: max_mv = 1500; break;   // 0-1.5V
        case ADC_ATTEN_DB_6:   max_mv = 2200; break;   // 0-2.2V
        case ADC_ATTEN_DB_12:  max_mv = 3300; break;   // 0-3.3V (renamed from DB_11)
        default: max_mv = 3300; break;
    }
    
    int max_val = (1 << s_pins[pin].analog.resolution) - 1;
    return (raw * max_mv) / max_val;
}

int gpio_analog_read_percent(int pin, int max_mv) {
    if (max_mv <= 0) max_mv = 3300;
    
    int mv = gpio_analog_read_millivolts(pin);
    if (mv < 0) return -1;
    
    int percent = (mv * 100) / max_mv;
    return (percent > 100) ? 100 : percent;
}

// =========================================================================
// PWM Output Functions
// =========================================================================

esp_err_t gpio_pwm_init(int pin, const pwm_config_t *config) {
    if (!is_valid_pin(pin) || !config) {
        ESP_LOGE(TAG, "Invalid pin or config");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Configure LEDC timer
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = config->resolution_bits,
        .timer_num = config->timer,
        .freq_hz = config->frequency_hz,
        .clk_cfg = LEDC_AUTO_CLK
    };
    
    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC timer: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure LEDC channel
    ledc_channel_config_t channel_conf = {
        .gpio_num = pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = config->channel,
        .timer_sel = config->timer,
        .duty = (config->duty_cycle_percent * ((1 << config->resolution_bits) - 1)) / 100,
        .hpoint = 0
    };
    
    ret = ledc_channel_config(&channel_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_pins[pin].mode = GPIO_CTRL_MODE_PWM_OUTPUT;
    s_pins[pin].configured = true;
    s_pins[pin].pwm.channel = config->channel;
    s_pins[pin].pwm.timer = config->timer;
    s_pins[pin].pwm.frequency = config->frequency_hz;
    s_pins[pin].pwm.resolution = config->resolution_bits;
    
    ESP_LOGI(TAG, "Pin %d configured as PWM output (%lu Hz, %d-bit)", 
             pin, config->frequency_hz, config->resolution_bits);
    return ESP_OK;
}

esp_err_t gpio_pwm_set_duty(int pin, uint8_t duty_percent) {
    if (!is_valid_pin(pin) || !s_pins[pin].configured) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_pins[pin].mode != GPIO_CTRL_MODE_PWM_OUTPUT && s_pins[pin].mode != GPIO_CTRL_MODE_SERVO_OUTPUT) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (duty_percent > 100) duty_percent = 100;
    
    uint32_t max_duty = (1 << s_pins[pin].pwm.resolution) - 1;
    uint32_t duty = (duty_percent * max_duty) / 100;
    
    return ledc_set_duty(LEDC_LOW_SPEED_MODE, s_pins[pin].pwm.channel, duty);
}

esp_err_t gpio_pwm_set_frequency(int pin, uint32_t frequency_hz) {
    if (!is_valid_pin(pin) || !s_pins[pin].configured) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_pins[pin].mode != GPIO_CTRL_MODE_PWM_OUTPUT && s_pins[pin].mode != GPIO_CTRL_MODE_SERVO_OUTPUT) {
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = ledc_set_freq(LEDC_LOW_SPEED_MODE, s_pins[pin].pwm.timer, frequency_hz);
    if (ret == ESP_OK) {
        s_pins[pin].pwm.frequency = frequency_hz;
    }
    return ret;
}

esp_err_t gpio_pwm_start(int pin) {
    if (!is_valid_pin(pin) || !s_pins[pin].configured) {
        return ESP_ERR_INVALID_STATE;
    }
    
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, s_pins[pin].pwm.channel);
}

esp_err_t gpio_pwm_stop(int pin) {
    if (!is_valid_pin(pin) || !s_pins[pin].configured) {
        return ESP_ERR_INVALID_STATE;
    }
    
    return ledc_stop(LEDC_LOW_SPEED_MODE, s_pins[pin].pwm.channel, 0);
}

// =========================================================================
// Servo Control Functions
// =========================================================================

esp_err_t gpio_servo_init(int pin, const servo_config_t *config) {
    if (!is_valid_pin(pin) || !config) {
        ESP_LOGE(TAG, "Invalid pin or config");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Servo typically uses 50Hz (20ms period)
    // Configure LEDC timer for 50Hz
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_16_BIT,  // 16-bit for precise pulse control
        .timer_num = config->timer,
        .freq_hz = 50,  // 50Hz for standard servo
        .clk_cfg = LEDC_AUTO_CLK
    };
    
    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure servo timer: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure LEDC channel
    ledc_channel_config_t channel_conf = {
        .gpio_num = pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = config->channel,
        .timer_sel = config->timer,
        .duty = 0,  // Will be set by gpio_servo_set_angle
        .hpoint = 0
    };
    
    ret = ledc_channel_config(&channel_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure servo channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_pins[pin].mode = GPIO_CTRL_MODE_SERVO_OUTPUT;
    s_pins[pin].configured = true;
    s_pins[pin].servo.channel = config->channel;
    s_pins[pin].servo.timer = config->timer;
    s_pins[pin].servo.min_pulse = config->min_pulse_us;
    s_pins[pin].servo.max_pulse = config->max_pulse_us;
    s_pins[pin].servo.min_angle = config->min_angle;
    s_pins[pin].servo.max_angle = config->max_angle;
    
    // Set to middle position
    uint16_t mid_angle = (config->min_angle + config->max_angle) / 2;
    ret = gpio_servo_set_angle(pin, mid_angle);
    
    ESP_LOGI(TAG, "Pin %d configured as servo output (%d-%d us, %d-%d degrees)", 
             pin, config->min_pulse_us, config->max_pulse_us, 
             config->min_angle, config->max_angle);
    return ret;
}

esp_err_t gpio_servo_set_angle(int pin, uint16_t angle) {
    if (!is_valid_pin(pin) || !s_pins[pin].configured) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_pins[pin].mode != GPIO_CTRL_MODE_SERVO_OUTPUT) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Clamp angle to configured range
    if (angle < s_pins[pin].servo.min_angle) angle = s_pins[pin].servo.min_angle;
    if (angle > s_pins[pin].servo.max_angle) angle = s_pins[pin].servo.max_angle;
    
    // Calculate pulse width
    uint16_t range = s_pins[pin].servo.max_angle - s_pins[pin].servo.min_angle;
    uint16_t pulse_range = s_pins[pin].servo.max_pulse - s_pins[pin].servo.min_pulse;
    uint16_t pulse = s_pins[pin].servo.min_pulse + 
                     ((angle - s_pins[pin].servo.min_angle) * pulse_range) / range;
    
    return gpio_servo_set_pulse(pin, pulse);
}

esp_err_t gpio_servo_set_pulse(int pin, uint16_t pulse_us) {
    if (!is_valid_pin(pin) || !s_pins[pin].configured) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_pins[pin].mode != GPIO_CTRL_MODE_SERVO_OUTPUT) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Convert pulse width to duty cycle
    // Servo period is 20ms (20000us)
    // Duty = (pulse_us / 20000) * (2^16 - 1)
    uint32_t max_duty = (1 << 16) - 1;  // 16-bit resolution
    uint32_t duty = (pulse_us * max_duty) / 20000;
    
    return ledc_set_duty(LEDC_LOW_SPEED_MODE, s_pins[pin].servo.channel, duty);
}

// =========================================================================
// Interrupt Functions
// =========================================================================

static void IRAM_ATTR gpio_isr_handler(void *arg) {
    int pin = (int)arg;
    if (is_valid_pin(pin) && s_pins[pin].configured && s_pins[pin].interrupt.callback) {
        s_pins[pin].interrupt.callback(s_pins[pin].interrupt.arg);
    }
}

esp_err_t gpio_isr_service_install(int max_handlers) {
    if (s_isr_installed) {
        ESP_LOGW(TAG, "ISR service already installed");
        return ESP_OK;
    }
    
    esp_err_t ret = gpio_install_isr_service(0);
    if (ret == ESP_OK) {
        s_isr_installed = true;
        ESP_LOGI(TAG, "GPIO ISR service installed");
    }
    return ret;
}

esp_err_t gpio_isr_service_uninstall(void) {
    if (!s_isr_installed) {
        return ESP_OK;
    }
    
    gpio_uninstall_isr_service();
    s_isr_installed = false;
    ESP_LOGI(TAG, "GPIO ISR service uninstalled");
    return ESP_OK;
}

esp_err_t gpio_interrupt_init(int pin, gpio_pull_t pull, gpio_ctrl_intr_type_t trigger,
                              gpio_isr_callback_t callback, void *arg) {
    if (!is_valid_pin(pin) || !callback) {
        ESP_LOGE(TAG, "Invalid pin or callback");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Install ISR service if not already installed
    if (!s_isr_installed) {
        esp_err_t ret = gpio_isr_service_install(8);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    
    // Map our interrupt type to ESP-IDF
    gpio_int_type_t intr_type;
    switch (trigger) {
        case GPIO_CTRL_INTR_RISING_EDGE:  intr_type = GPIO_INTR_POSEDGE; break;
        case GPIO_CTRL_INTR_FALLING_EDGE: intr_type = GPIO_INTR_NEGEDGE; break;
        case GPIO_CTRL_INTR_BOTH_EDGES:   intr_type = GPIO_INTR_ANYEDGE; break;
        case GPIO_CTRL_INTR_LOW_LEVEL:    intr_type = GPIO_INTR_LOW_LEVEL; break;
        case GPIO_CTRL_INTR_HIGH_LEVEL:   intr_type = GPIO_INTR_HIGH_LEVEL; break;
        default:                          intr_type = GPIO_INTR_DISABLE; break;
    }
    
    // Configure pin as input with interrupt
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = (pull == GPIO_PULL_UP || pull == GPIO_PULL_BOTH) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = (pull == GPIO_PULL_DOWN || pull == GPIO_PULL_BOTH) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = intr_type
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure interrupt pin %d: %s", pin, esp_err_to_name(ret));
        return ret;
    }
    
    // Add ISR handler
    ret = gpio_isr_handler_add(pin, gpio_isr_handler, (void *)pin);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add ISR handler for pin %d: %s", pin, esp_err_to_name(ret));
        return ret;
    }
    
    s_pins[pin].mode = GPIO_CTRL_MODE_DIGITAL_INPUT;
    s_pins[pin].configured = true;
    s_pins[pin].interrupt.callback = callback;
    s_pins[pin].interrupt.arg = arg;
    s_pins[pin].interrupt.trigger = trigger;
    
    ESP_LOGI(TAG, "Pin %d configured for interrupt (trigger: %d)", pin, trigger);
    return ESP_OK;
}

// =========================================================================
// Utility Functions
// =========================================================================

bool gpio_is_adc_capable(int pin) {
    return is_valid_adc1_pin(pin) || is_valid_adc2_pin(pin);
}

bool gpio_is_pwm_capable(int pin) {
    // All GPIO pins on ESP32 support PWM via LEDC
    return is_valid_pin(pin);
}

int gpio_get_adc_unit(int pin) {
    if (is_valid_adc1_pin(pin)) return ADC_UNIT_1;
    if (is_valid_adc2_pin(pin)) return ADC_UNIT_2;
    return -1;
}

int gpio_get_adc_channel(int pin) {
    for (int i = 0; adc1_pins[i].gpio != -1; i++) {
        if (adc1_pins[i].gpio == pin) return (int)adc1_pins[i].channel;
    }
    for (int i = 0; adc2_pins[i].gpio != -1; i++) {
        if (adc2_pins[i].gpio == pin) return (int)adc2_pins[i].channel;
    }
    return -1;
}

bool gpio_adc_channel_is_configured(adc_unit_t unit, adc_channel_t channel) {
    if (unit == ADC_UNIT_1 && channel < 10) {
        return s_adc1_channels_configured[channel];
    } else if (unit == ADC_UNIT_2 && channel < 10) {
        return s_adc2_channels_configured[channel];
    }
    return false;
}