/**
 * BLE LED Controller for XIAO ESP32-C6
 *
 * GATT Server with LED Control Service
 * - Service UUID: 12345678-1234-5678-1234-56789abcdef0
 * - LED Characteristic UUID: 12345678-1234-5678-1234-56789abcdef1
 * - LED GPIO: 15 (XIAO ESP32-C6 internal LED)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "driver/gpio.h"

#define TAG "LED_BLE"

// ============================================================================
// Configuration
// ============================================================================

#define DEVICE_NAME         "ESP-LED-CTRL"
#define LED_GPIO            15      // XIAO ESP32-C6 internal LED
#define APP_ID              0x55

// LED state values
#define LED_OFF             0x00
#define LED_ON              0x01

// ============================================================================
// UUIDs (stored in little-endian for ESP-IDF)
// ============================================================================

// Service UUID: 12345678-1234-5678-1234-56789abcdef0
static const uint8_t led_service_uuid[16] = {
    0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
    0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
};

// Characteristic UUID: 12345678-1234-5678-1234-56789abcdef1
static const uint8_t led_char_uuid[16] = {
    0xf1, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
    0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12
};

// Standard UUIDs for GATT declarations
static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t char_declare_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint8_t char_prop_read_write = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE;

// ============================================================================
// GATT Database
// ============================================================================

// Attribute table indices
enum {
    IDX_SVC,            // Service declaration
    IDX_LED_CHAR,       // Characteristic declaration
    IDX_LED_CHAR_VAL,   // Characteristic value
    LED_IDX_NB          // Total number of handles
};

// Global state
static uint8_t led_state = LED_OFF;
static uint16_t handle_table[LED_IDX_NB];
static esp_gatt_if_t gatts_if_for_profiles = ESP_GATT_IF_NONE;

// GATT Database using service table approach
static const esp_gatts_attr_db_t gatt_db[LED_IDX_NB] = {
    // Service Declaration
    [IDX_SVC] = {
        {ESP_GATT_AUTO_RSP},
        {
            ESP_UUID_LEN_16,
            (uint8_t *)&primary_service_uuid,
            ESP_GATT_PERM_READ,
            sizeof(led_service_uuid),
            sizeof(led_service_uuid),
            (uint8_t *)led_service_uuid
        }
    },
    // LED Characteristic Declaration
    [IDX_LED_CHAR] = {
        {ESP_GATT_AUTO_RSP},
        {
            ESP_UUID_LEN_16,
            (uint8_t *)&char_declare_uuid,
            ESP_GATT_PERM_READ,
            sizeof(uint8_t),
            sizeof(uint8_t),
            (uint8_t *)&char_prop_read_write
        }
    },
    // LED Characteristic Value
    [IDX_LED_CHAR_VAL] = {
        {ESP_GATT_AUTO_RSP},
        {
            ESP_UUID_LEN_128,
            (uint8_t *)led_char_uuid,
            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            sizeof(uint8_t),
            sizeof(uint8_t),
            &led_state
        }
    },
};

// ============================================================================
// Advertising Configuration
// ============================================================================

static uint8_t adv_config_done = 0;
#define ADV_CONFIG_FLAG      (1 << 0)
#define SCAN_RSP_CONFIG_FLAG (1 << 1)

// Advertising parameters
static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x20,     // 20ms minimum interval
    .adv_int_max        = 0x40,     // 40ms maximum interval
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// Raw advertising data with 128-bit service UUID
static uint8_t raw_adv_data[] = {
    // Flags
    0x02, ESP_BLE_AD_TYPE_FLAG, 0x06,
    // Complete 128-bit Service UUID
    0x11, ESP_BLE_AD_TYPE_128SRV_CMPL,
    0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
    0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12,
};

// Scan response with device name
static uint8_t raw_scan_rsp_data[] = {
    // Complete Local Name: "ESP-LED-CTRL"
    0x0d, ESP_BLE_AD_TYPE_NAME_CMPL,
    'E', 'S', 'P', '-', 'L', 'E', 'D', '-', 'C', 'T', 'R', 'L'
};

// ============================================================================
// LED Control Functions
// ============================================================================

static void led_init(void) {
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, led_state);
    ESP_LOGI(TAG, "LED initialized on GPIO %d", LED_GPIO);
}

static void led_set_state(uint8_t state) {
    led_state = (state != 0) ? LED_ON : LED_OFF;
    gpio_set_level(LED_GPIO, led_state);
    ESP_LOGI(TAG, "LED state set to: %s", led_state ? "ON" : "OFF");
}

// ============================================================================
// GAP Event Handler
// ============================================================================

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
            adv_config_done &= (~ADV_CONFIG_FLAG);
            if (adv_config_done == 0) {
                esp_ble_gap_start_advertising(&adv_params);
            }
            break;

        case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
            adv_config_done &= (~SCAN_RSP_CONFIG_FLAG);
            if (adv_config_done == 0) {
                esp_ble_gap_start_advertising(&adv_params);
            }
            break;

        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Advertising started - device is discoverable");
            } else {
                ESP_LOGE(TAG, "Advertising start failed, status=%d", param->adv_start_cmpl.status);
            }
            break;

        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            if (param->adv_stop_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Advertising stopped");
            }
            break;

        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
            ESP_LOGI(TAG, "Connection params updated: interval=%d, latency=%d, timeout=%d",
                     param->update_conn_params.conn_int,
                     param->update_conn_params.latency,
                     param->update_conn_params.timeout);
            break;

        default:
            break;
    }
}

// ============================================================================
// GATT Server Event Handler
// ============================================================================

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                                 esp_ble_gatts_cb_param_t *param) {
    switch (event) {
        case ESP_GATTS_REG_EVT:
            if (param->reg.status == ESP_GATT_OK) {
                gatts_if_for_profiles = gatts_if;
                ESP_LOGI(TAG, "GATT server registered, app_id=%d, gatts_if=%d",
                         param->reg.app_id, gatts_if);

                // Set device name
                esp_ble_gap_set_device_name(DEVICE_NAME);

                // Configure advertising data
                esp_ble_gap_config_adv_data_raw(raw_adv_data, sizeof(raw_adv_data));
                adv_config_done |= ADV_CONFIG_FLAG;

                // Configure scan response data
                esp_ble_gap_config_scan_rsp_data_raw(raw_scan_rsp_data, sizeof(raw_scan_rsp_data));
                adv_config_done |= SCAN_RSP_CONFIG_FLAG;

                // Create attribute table
                esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, LED_IDX_NB, 0);
            } else {
                ESP_LOGE(TAG, "GATT server registration failed, status=%d", param->reg.status);
            }
            break;

        case ESP_GATTS_CREAT_ATTR_TAB_EVT:
            if (param->add_attr_tab.status == ESP_GATT_OK) {
                if (param->add_attr_tab.num_handle == LED_IDX_NB) {
                    memcpy(handle_table, param->add_attr_tab.handles, sizeof(handle_table));
                    esp_ble_gatts_start_service(handle_table[IDX_SVC]);
                    ESP_LOGI(TAG, "Service created successfully, starting service...");
                } else {
                    ESP_LOGE(TAG, "Attribute table size mismatch: expected %d, got %d",
                             LED_IDX_NB, param->add_attr_tab.num_handle);
                }
            } else {
                ESP_LOGE(TAG, "Create attribute table failed, status=%d", param->add_attr_tab.status);
            }
            break;

        case ESP_GATTS_START_EVT:
            if (param->start.status == ESP_GATT_OK) {
                ESP_LOGI(TAG, "Service started, handle=0x%04x", param->start.service_handle);
            }
            break;

        case ESP_GATTS_READ_EVT:
            ESP_LOGI(TAG, "Read request: handle=0x%04x, conn_id=%d",
                     param->read.handle, param->read.conn_id);
            // Auto-response handles read automatically
            break;

        case ESP_GATTS_WRITE_EVT:
            ESP_LOGI(TAG, "Write request: handle=0x%04x, len=%d, need_rsp=%d",
                     param->write.handle, param->write.len, param->write.need_rsp);

            if (param->write.handle == handle_table[IDX_LED_CHAR_VAL]) {
                if (param->write.len == 1) {
                    led_set_state(param->write.value[0]);
                    // Update the stored attribute value
                    esp_ble_gatts_set_attr_value(handle_table[IDX_LED_CHAR_VAL], 1, &led_state);
                } else {
                    ESP_LOGW(TAG, "Invalid write length: expected 1, got %d", param->write.len);
                }
            }

            // Send response if needed
            if (param->write.need_rsp) {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                            param->write.trans_id, ESP_GATT_OK, NULL);
            }
            break;

        case ESP_GATTS_CONNECT_EVT:
            ESP_LOGI(TAG, "Client connected: conn_id=%d, remote_bda=" ESP_BD_ADDR_STR,
                     param->connect.conn_id, ESP_BD_ADDR_HEX(param->connect.remote_bda));

            // Update connection parameters for better iOS compatibility
            esp_ble_conn_update_params_t conn_params = {0};
            memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            conn_params.min_int = 0x10;     // 20ms
            conn_params.max_int = 0x20;     // 40ms
            conn_params.latency = 0;
            conn_params.timeout = 400;      // 4000ms
            esp_ble_gap_update_conn_params(&conn_params);
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(TAG, "Client disconnected: conn_id=%d, reason=0x%02x",
                     param->disconnect.conn_id, param->disconnect.reason);
            // Restart advertising after disconnect
            esp_ble_gap_start_advertising(&adv_params);
            break;

        case ESP_GATTS_MTU_EVT:
            ESP_LOGI(TAG, "MTU exchanged: conn_id=%d, mtu=%d",
                     param->mtu.conn_id, param->mtu.mtu);
            break;

        default:
            break;
    }
}

// ============================================================================
// Main Application
// ============================================================================

void app_main(void) {
    esp_err_t ret;

    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "BLE LED Controller for XIAO ESP32-C6");
    ESP_LOGI(TAG, "====================================");

    // Initialize NVS (required for BLE)
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize LED
    led_init();

    // Release memory for classic Bluetooth (not used on ESP32-C6)
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    // Initialize Bluetooth controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "Bluetooth controller init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "Bluetooth controller enable failed: %s", esp_err_to_name(ret));
        return;
    }

    // Initialize Bluedroid stack
    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ret = esp_bluedroid_init_with_cfg(&bluedroid_cfg);
    if (ret) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return;
    }

    // Register GATT server callback
    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret) {
        ESP_LOGE(TAG, "GATTS register callback failed: %s", esp_err_to_name(ret));
        return;
    }

    // Register GAP callback
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret) {
        ESP_LOGE(TAG, "GAP register callback failed: %s", esp_err_to_name(ret));
        return;
    }

    // Register GATT server application
    ret = esp_ble_gatts_app_register(APP_ID);
    if (ret) {
        ESP_LOGE(TAG, "GATTS app register failed: %s", esp_err_to_name(ret));
        return;
    }

    // Set MTU size
    esp_ble_gatt_set_local_mtu(500);

    ESP_LOGI(TAG, "Initialization complete");
    ESP_LOGI(TAG, "Device name: %s", DEVICE_NAME);
    ESP_LOGI(TAG, "Service UUID: 12345678-1234-5678-1234-56789abcdef0");
}
