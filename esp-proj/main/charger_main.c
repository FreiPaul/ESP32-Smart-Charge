/**
 * Smart Charger Controller for XIAO ESP32-C6
 *
 * Main application entry point.
 * Initializes hardware, BLE, and creates the reconcile task.
 */

#include <stdio.h>

#include "charger_fsm.h"
#include "charger_gatt.h"
#include "charger_hardware.h"
#include "charger_time.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char* TAG = "MAIN";

/**
 * Reconcile task - runs every 1 second
 *
 * - Increments internal time
 * - Runs FSM reconciliation
 * - Sends status notification to connected client
 */
static void reconcile_task(void* pvParameters) {
    const TickType_t delay = pdMS_TO_TICKS(1000);  // 1 second

    ESP_LOGI(TAG, "Reconcile task started");

    while (1) {
        // Increment internal time
        time_tick();

        // Run FSM reconciliation
        fsm_reconcile();

        // Send status notification (if client is connected and subscribed)
        charger_gatt_notify_status();

        vTaskDelay(delay);
    }
}

void app_main(void) {
    esp_err_t ret;

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Smart Charger Controller - XIAO ESP32-C6");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Relay GPIO: %d", RELAY_GPIO);
    ESP_LOGI(TAG, "Voltage ADC GPIO: %d", VOLTAGE_ADC_GPIO);
    ESP_LOGI(TAG, "Voltage Divider Ratio: %.1f", VOLTAGE_DIVIDER_RATIO);

    // Initialize NVS (required for BLE)
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize hardware (relay GPIO, ADC)
    hardware_init();

    // Initialize FSM
    fsm_init();

    // Initialize GATT server and start advertising
    charger_gatt_init();

    // Create reconcile task (1 Hz)
    xTaskCreate(reconcile_task, "reconcile", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Initialization complete");
    ESP_LOGI(TAG, "Device name: %s", DEVICE_NAME);
    ESP_LOGI(TAG, "Waiting for BLE connection...");
}
