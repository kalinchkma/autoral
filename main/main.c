#include <stdio.h>
#include "app_logging.h"
#include "system/system_init.h"
#include "connectivity/wifi_manager.h"
#include "connectivity/mqtt_manager.h"
#include "config/app_config.h"
#include "config/credentials.h"
#include "system/app_task.h"
#include "connectivity/ble_mesh_manager.h"
#include "control/engine.h"
#include "control/gpio_control.h"

static const char* TAG = "MAIN";

/* ── Engine source/target IDs ────────────────────────────────────────── */
#define SRC_LIGHT_SENSOR    1   /* ADC analog input (light level)       */
#define SRC_POT_SENSOR      2   /* ADC analog input (potentiometer)     */
#define TGT_LIGHT_OUT       1   /* GPIO output (light relay)            */
#define TGT_AC_OUT          2   /* GPIO output (AC relay)               */
#define TGT_FAN_OUT         3   /* GPIO output (fan relay)              */
#define TGT_LED_PWM         4   /* GPIO PWM output (LED brightness)     */
#define PARAM_THRESHOLD     1   /* Tunable threshold value              */
#define PARAM_MODE          2   /* Operating mode (0=off, 1=auto, 2=on) */

/* ── Hardware pin mapping ────────────────────────────────────────────── */
#define PIN_LIGHT_RELAY     2
#define PIN_AC_RELAY        4
#define PIN_FAN_RELAY       16
#define PIN_LED_PWM         17
#define PIN_LIGHT_SENSOR    34  /* ADC1_CH6 — analog light sensor */
#define PIN_POT_SENSOR      35  /* ADC1_CH7 — analog potentiometer */

/* ── Source read callbacks (called by engine each eval cycle) ────────── */

static float read_light_sensor(void *user_data)
{
    int raw = gpio_analog_read_millivolts(PIN_LIGHT_SENSOR);
    if (raw < 0) return 0;
    /* Convert mV (0–3300) to percentage (0–100) */
    return (float)raw / 33.0f;
}

static float read_pot_sensor(void *user_data)
{
    int raw = gpio_analog_read_millivolts(PIN_POT_SENSOR);
    if (raw < 0) return 0;
    /* Convert mV (0–3300) to percentage (0–100) */
    return (float)raw / 33.0f;
}

/* ── Target write callbacks (called by engine when rule triggers) ────── */

typedef struct {
    int pin;
    int last_state;
} gpio_target_ctx_t;

static gpio_target_ctx_t s_light_ctx  = { .pin = PIN_LIGHT_RELAY, .last_state = -1 };
static gpio_target_ctx_t s_ac_ctx     = { .pin = PIN_AC_RELAY,    .last_state = -1 };
static gpio_target_ctx_t s_fan_ctx    = { .pin = PIN_FAN_RELAY,   .last_state = -1 };

static esp_err_t write_gpio_relay(float value, void *user_data)
{
    gpio_target_ctx_t *ctx = (gpio_target_ctx_t *)user_data;
    int state = (value > 0.5f) ? PIN_HIGH : PIN_LOW;

    /* Only write if state actually changed */
    if (ctx->last_state == state) return ESP_OK;

    esp_err_t err = gpio_digital_write(ctx->pin, state);
    if (err == ESP_OK) {
        ctx->last_state = state;
        APP_LOGI(TAG, "GPIO %d → %s", ctx->pin, state ? "HIGH" : "LOW");
    }
    return err;
}

static esp_err_t write_pwm_led(float value, void *user_data)
{
    /* value = 0–100 (duty cycle percentage) */
    uint8_t duty = (uint8_t)(value > 100 ? 100 : (value < 0 ? 0 : value));
    esp_err_t err = gpio_pwm_set_duty(PIN_LED_PWM, duty);
    if (err == ESP_OK) {
        APP_LOGI(TAG, "LED PWM → %d%%", duty);
    }
    return err;
}


void app_main(void)
{   
    APP_LOGI(TAG, "=====================================================================");
    APP_LOGI(TAG, "Application started");
    APP_LOGI(TAG, "=====================================================================");


    // =========================================================================
    // Initialize logging
    // =========================================================================
    app_logging_init();

    // =========================================================================
    // Initialize system (NVS, GPIO, etc.)
    // =========================================================================
    esp_err_t ret = system_init();
    if (ret != ESP_OK) {
        APP_LOGE(TAG, "System initialization failed");
        return;
    }


    // =========================================================================
    // Initialize Credential Store + set defaults
    // =========================================================================
    ret = cred_init();
    if (ret != ESP_OK) {
        APP_LOGE(TAG, "Credential store init failed");
        return;
    }

    /* Set default WiFi credentials if none are stored.
     * Later these will come via BLE Mesh provisioning. */
    if (!cred_wifi_is_set()) {
        cred_wifi_t wifi_cred = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        };
        cred_wifi_set(&wifi_cred);
        APP_LOGI(TAG, "Default WiFi credentials loaded");
    }

    /* Set default MQTT credentials if none are stored.
     * Later these will come via BLE Mesh provisioning. */
    if (!cred_mqtt_is_set()) {
        cred_mqtt_t mqtt_cred = {
            .uri = MQTT_BROKER_URI,
            .client_id = MQTT_CLIENT_ID,
            .username = MQTT_USERNAME,
            .password = MQTT_PASSWORD,
            .keepalive = MQTT_KEEPALIVE,
        };
        strncpy(mqtt_cred.topic_commands,  MQTT_TOPIC_COMMANDS,  sizeof(mqtt_cred.topic_commands) - 1);
        strncpy(mqtt_cred.topic_status,    MQTT_TOPIC_STATUS,    sizeof(mqtt_cred.topic_status) - 1);
        strncpy(mqtt_cred.topic_telemetry, MQTT_TOPIC_TELEMETRY, sizeof(mqtt_cred.topic_telemetry) - 1);
        cred_mqtt_set(&mqtt_cred);
        APP_LOGI(TAG, "Default MQTT credentials loaded");
    }

    // =========================================================================
    // Initialize WIFI
    // =========================================================================
    ret = wifi_init_sta();
    if (ret != ESP_OK) {
        APP_LOGE(TAG, "WIFI initialization failed");
        return;
    }

    // =========================================================================
    // Initialize BLE Mesh
    // =========================================================================
    ret = mesh_init();
    if (ret != ESP_OK) 
    {
        APP_LOGE(TAG, "Failed to initialized mesh");
        return;
    }

    /* ── Initialize GPIO hardware FIRST ───────────────────────────────── */
    gpio_control_init();

    /* Digital outputs (relays) */
    gpio_digital_output_init(PIN_LIGHT_RELAY, PIN_LOW, GPIO_PULL_NONE);
    gpio_digital_output_init(PIN_AC_RELAY,    PIN_LOW, GPIO_PULL_NONE);
    gpio_digital_output_init(PIN_FAN_RELAY,   PIN_LOW, GPIO_PULL_NONE);

    /* PWM output (LED) */
    pwm_config_t led_pwm = {
        .frequency_hz = 1000,
        .resolution_bits = 8,
        .duty_cycle_percent = 0,
        .channel = LEDC_CHANNEL_0,
        .timer = LEDC_TIMER_0,
    };
    gpio_pwm_init(PIN_LED_PWM, &led_pwm);
    gpio_pwm_start(PIN_LED_PWM);

    /* Analog inputs (sensors) */
    adc_config_t light_adc = {
        .unit = ADC_UNIT_1,
        .channel = ADC_CHANNEL_6,   /* GPIO 34 */
        .resolution = ADC_RESOLUTION_12_BIT,
        .attenuation = ADC_ATTEN_DB_12,
    };
    gpio_analog_init(&light_adc);

    adc_config_t pot_adc = {
        .unit = ADC_UNIT_1,
        .channel = ADC_CHANNEL_7,   /* GPIO 35 */
        .resolution = ADC_RESOLUTION_12_BIT,
        .attenuation = ADC_ATTEN_DB_12,
    };
    gpio_analog_init(&pot_adc);

    APP_LOGI(TAG, "GPIO hardware initialized");

    // =========================================================================
    // Initialize Control Engine (AFTER GPIO hardware is ready)
    // =========================================================================
    ret = engine_init();
    if (ret != ESP_OK) {
        APP_LOGE(TAG, "Failed to initialize engine: %s", esp_err_to_name(ret));
        return;
    }

    /* ── Register engine sources (sensors) ────────────────────────────── */
    engine_register_source(SRC_LIGHT_SENSOR, "light_pct",
                           read_light_sensor, NULL);
    engine_register_source(SRC_POT_SENSOR, "pot_pct",
                           read_pot_sensor, NULL);

    /* ── Register engine targets (actuators) ──────────────────────────── */
    engine_register_target(TGT_LIGHT_OUT, "light_relay",
                           write_gpio_relay, &s_light_ctx);
    engine_register_target(TGT_AC_OUT, "ac_relay",
                           write_gpio_relay, &s_ac_ctx);
    engine_register_target(TGT_FAN_OUT, "fan_relay",
                           write_gpio_relay, &s_fan_ctx);
    engine_register_target(TGT_LED_PWM, "led_pwm",
                           write_pwm_led, NULL);

    /* ── Register engine params (tunable, persisted to NVS) ───────────── */
    engine_register_param(PARAM_THRESHOLD, "threshold",
                          50.0f, 0.0f, 100.0f);
    engine_register_param(PARAM_MODE, "mode",
                          1.0f, 0.0f, 3.0f);

    APP_LOGI(TAG, "Engine hardware registered: 2 sources, 4 targets, 2 params");

    /* No default rules — all rules are created via MQTT / BLE Mesh / Web UI.
     * Rules persist across reboots via NVS. */
    APP_LOGI(TAG, "Rules on boot: %u loaded from NVS (max %u)",
             engine_rule_get_count(), engine_get_max_rules());

    // Initialize application tasks
    ret = app_task_init();
    if (ret != ESP_OK) {
        APP_LOGE(TAG, "Failed to initialize application tasks");
        return;
    }
}
