/**
 * Charger Hardware Implementation
 */

#include "charger_hardware.h"

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char* TAG = "HW";

// ADC handle
static adc_oneshot_unit_handle_t adc_handle = NULL;

// Current relay state
static bool relay_state = false;

void hardware_init(void) {
    esp_err_t ret;

    // Initialize relay GPIO
    gpio_config_t relay_conf = {
        .pin_bit_mask = (1ULL << RELAY_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&relay_conf);
    gpio_set_level(RELAY_GPIO,
                   INVERT_RELAY_LOGIC ? 1 : 0);  // Start with relay OFF
    relay_state = false;
    ESP_LOGI(TAG, "Relay initialized on GPIO %d", RELAY_GPIO);

    // Initialize ADC for voltage reading
    adc_oneshot_unit_init_cfg_t adc_config = {
        .unit_id = ADC_UNIT_1,
    };
    ret = adc_oneshot_new_unit(&adc_config, &adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC unit init failed: %s", esp_err_to_name(ret));
        return;
    }

    adc_oneshot_chan_cfg_t chan_config = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_WIDTH,
    };
    ret = adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_0, &chan_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel config failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "ADC initialized on GPIO %d (CH0), divider ratio %.1f",
             VOLTAGE_ADC_GPIO, VOLTAGE_DIVIDER_RATIO);
}

void relay_set(bool on) {
    relay_state = on;
    if (INVERT_RELAY_LOGIC) {
        on = !on;
    }
    gpio_set_level(RELAY_GPIO, on ? 1 : 0);
    ESP_LOGI(TAG, "Relay %s", on ? "ON" : "OFF");
}

bool relay_get_state(void) { return relay_state; }

uint16_t voltage_read_mv(void) {
    if (adc_handle == NULL) {
        ESP_LOGW(TAG, "ADC not initialized");
        return 0;
    }

    int32_t raw_sum = 0;
    int valid_samples = 0;

    for (int i = 0; i < 16; i++) {
        int raw_value = 0;
        esp_err_t ret = adc_oneshot_read(adc_handle, ADC_CHANNEL_0, &raw_value);
        if (ret == ESP_OK) {
            raw_sum += raw_value;
            valid_samples++;
        }
    }

    if (valid_samples == 0) {
        ESP_LOGW(TAG, "ADC read failed: no valid samples");
        return 0;
    }

    int raw_avg = raw_sum / valid_samples;

    // Convert raw ADC value to voltage
    // ADC reference voltage is ~3.3V with 12-bit resolution (0-4095)
    // adc_voltage_mv = (raw / 4095) * 3300
    // actual_voltage_mv = adc_voltage_mv * VOLTAGE_DIVIDER_RATIO
    float adc_voltage_mv = (raw_avg / 4095.0f) * 3300.0f;
    float actual_voltage_mv = adc_voltage_mv * VOLTAGE_DIVIDER_RATIO;

    return (uint16_t)actual_voltage_mv;
}
