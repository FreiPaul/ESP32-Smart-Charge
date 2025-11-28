/**
 * Charger GATT Server Implementation
 */

#include "charger_gatt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "charger_fsm.h"
#include "charger_settings.h"
#include "driver/gpio.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gatts_api.h"
#include "esp_log.h"

static const char* TAG = "GATT";

// ============================================================================
// UUIDs (stored in little-endian for ESP-IDF)
// ============================================================================

// Service UUID: 12345678-1234-5678-1234-56789abcdef0
static const uint8_t service_uuid[16] = {0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56,
                                         0x34, 0x12, 0x78, 0x56, 0x34, 0x12,
                                         0x78, 0x56, 0x34, 0x12};

// Settings Characteristic UUID: 12345678-1234-5678-1234-56789abcdef2
static const uint8_t settings_char_uuid[16] = {
    0xf2, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
    0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12};

// Status Characteristic UUID: 12345678-1234-5678-1234-56789abcdef3
static const uint8_t status_char_uuid[16] = {0xf3, 0xde, 0xbc, 0x9a, 0x78, 0x56,
                                             0x34, 0x12, 0x78, 0x56, 0x34, 0x12,
                                             0x78, 0x56, 0x34, 0x12};

// Command Characteristic UUID: 12345678-1234-5678-1234-56789abcdef4
static const uint8_t command_char_uuid[16] = {
    0xf4, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
    0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12};

// Standard UUIDs
static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t char_declare_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t ccc_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

// Characteristic properties
static const uint8_t char_prop_write = ESP_GATT_CHAR_PROP_BIT_WRITE;
static const uint8_t char_prop_read_notify =
    ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;

// ============================================================================
// GATT Database
// ============================================================================

enum {
    IDX_SVC,
    // Settings characteristic (Write)
    IDX_SETTINGS_CHAR,
    IDX_SETTINGS_VAL,
    // Status characteristic (Read/Notify)
    IDX_STATUS_CHAR,
    IDX_STATUS_VAL,
    IDX_STATUS_CCC,
    // Command characteristic (Write)
    IDX_CMD_CHAR,
    IDX_CMD_VAL,

    CHARGER_IDX_NB
};

// Placeholder values for characteristic values
static uint8_t settings_val[SETTINGS_PACKET_SIZE] = {0};
static uint8_t status_val[STATUS_PACKET_SIZE] = {0};
static uint8_t command_val[1] = {0};
static uint16_t status_ccc = 0;

static const esp_gatts_attr_db_t gatt_db[CHARGER_IDX_NB] = {
    // Service Declaration
    [IDX_SVC] = {{ESP_GATT_AUTO_RSP},
                 {ESP_UUID_LEN_16, (uint8_t*)&primary_service_uuid,
                  ESP_GATT_PERM_READ, sizeof(service_uuid),
                  sizeof(service_uuid), (uint8_t*)service_uuid}},

    // Settings Characteristic Declaration
    [IDX_SETTINGS_CHAR] = {{ESP_GATT_AUTO_RSP},
                           {ESP_UUID_LEN_16, (uint8_t*)&char_declare_uuid,
                            ESP_GATT_PERM_READ, sizeof(uint8_t),
                            sizeof(uint8_t), (uint8_t*)&char_prop_write}},
    // Settings Characteristic Value
    [IDX_SETTINGS_VAL] = {{ESP_GATT_RSP_BY_APP},  // Manual response for write
                          {ESP_UUID_LEN_128, (uint8_t*)settings_char_uuid,
                           ESP_GATT_PERM_WRITE, sizeof(settings_val),
                           sizeof(settings_val), settings_val}},

    // Status Characteristic Declaration
    [IDX_STATUS_CHAR] = {{ESP_GATT_AUTO_RSP},
                         {ESP_UUID_LEN_16, (uint8_t*)&char_declare_uuid,
                          ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t),
                          (uint8_t*)&char_prop_read_notify}},
    // Status Characteristic Value
    [IDX_STATUS_VAL] = {{ESP_GATT_RSP_BY_APP},  // Manual response for read
                        {ESP_UUID_LEN_128, (uint8_t*)status_char_uuid,
                         ESP_GATT_PERM_READ, sizeof(status_val),
                         sizeof(status_val), status_val}},
    // Status CCC (Client Characteristic Configuration)
    [IDX_STATUS_CCC] = {{ESP_GATT_AUTO_RSP},
                        {ESP_UUID_LEN_16, (uint8_t*)&ccc_uuid,
                         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                         sizeof(uint16_t), sizeof(status_ccc),
                         (uint8_t*)&status_ccc}},

    // Command Characteristic Declaration
    [IDX_CMD_CHAR] = {{ESP_GATT_AUTO_RSP},
                      {ESP_UUID_LEN_16, (uint8_t*)&char_declare_uuid,
                       ESP_GATT_PERM_READ, sizeof(uint8_t), sizeof(uint8_t),
                       (uint8_t*)&char_prop_write}},
    // Command Characteristic Value
    [IDX_CMD_VAL] = {{ESP_GATT_RSP_BY_APP},  // Manual response for write
                     {ESP_UUID_LEN_128, (uint8_t*)command_char_uuid,
                      ESP_GATT_PERM_WRITE, sizeof(command_val),
                      sizeof(command_val), command_val}},
};

// ============================================================================
// Global State
// ============================================================================

static uint16_t handle_table[CHARGER_IDX_NB];
static esp_gatt_if_t g_gatts_if = ESP_GATT_IF_NONE;
static uint16_t g_conn_id = 0xFFFF;
static bool g_is_connected = false;

// ============================================================================
// Advertising Configuration
// ============================================================================

static uint8_t adv_config_done = 0;
#define ADV_CONFIG_FLAG (1 << 0)
#define SCAN_RSP_CONFIG_FLAG (1 << 1)

static esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static uint8_t raw_adv_data[] = {
    0x02,
    ESP_BLE_AD_TYPE_FLAG,
    0x06,
    0x11,
    ESP_BLE_AD_TYPE_128SRV_CMPL,
    0xf0,
    0xde,
    0xbc,
    0x9a,
    0x78,
    0x56,
    0x34,
    0x12,
    0x78,
    0x56,
    0x34,
    0x12,
    0x78,
    0x56,
    0x34,
    0x12,
};

static uint8_t raw_scan_rsp_data[] = {0x0d, ESP_BLE_AD_TYPE_NAME_CMPL,
                                      'E',  'S',
                                      'P',  '-',
                                      'C',  'H',
                                      'A',  'R',
                                      'G',  'E',
                                      'R'};

// ============================================================================
// GAP Event Handler
// ============================================================================

static void gap_event_handler(esp_gap_ble_cb_event_t event,
                              esp_ble_gap_cb_param_t* param) {
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
                ESP_LOGI(TAG, "Advertising started");
            } else {
                ESP_LOGE(TAG, "Advertising start failed: %d",
                         param->adv_start_cmpl.status);
            }
            break;

        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
            ESP_LOGI(TAG, "Connection params updated");
            break;

        default:
            break;
    }
}

// ============================================================================
// GATT Server Event Handler
// ============================================================================

static void gatts_event_handler(esp_gatts_cb_event_t event,
                                esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t* param) {
    switch (event) {
        case ESP_GATTS_REG_EVT:
            if (param->reg.status == ESP_GATT_OK) {
                g_gatts_if = gatts_if;
                ESP_LOGI(TAG, "GATT server registered");

                esp_ble_gap_set_device_name(DEVICE_NAME);
                esp_ble_gap_config_adv_data_raw(raw_adv_data,
                                                sizeof(raw_adv_data));
                adv_config_done |= ADV_CONFIG_FLAG;
                esp_ble_gap_config_scan_rsp_data_raw(raw_scan_rsp_data,
                                                     sizeof(raw_scan_rsp_data));
                adv_config_done |= SCAN_RSP_CONFIG_FLAG;

                esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, CHARGER_IDX_NB,
                                              0);
            }
            break;

        case ESP_GATTS_CREAT_ATTR_TAB_EVT:
            if (param->add_attr_tab.status == ESP_GATT_OK &&
                param->add_attr_tab.num_handle == CHARGER_IDX_NB) {
                memcpy(handle_table, param->add_attr_tab.handles,
                       sizeof(handle_table));
                esp_ble_gatts_start_service(handle_table[IDX_SVC]);
                ESP_LOGI(TAG, "Service created with %d handles",
                         CHARGER_IDX_NB);
            }
            break;

        case ESP_GATTS_START_EVT:
            ESP_LOGI(TAG, "Service started");
            break;

        case ESP_GATTS_READ_EVT: {
            ESP_LOGI(TAG, "Read request: handle=0x%04x", param->read.handle);

            if (param->read.handle == handle_table[IDX_STATUS_VAL]) {
                // Build current status
                charger_status_t status;
                fsm_get_status(&status);

                uint8_t data[STATUS_PACKET_SIZE];
                uint16_t len = status_serialize(&status, data, sizeof(data));

                esp_gatt_rsp_t rsp;
                memset(&rsp, 0, sizeof(rsp));
                rsp.attr_value.handle = param->read.handle;
                rsp.attr_value.len = len;
                memcpy(rsp.attr_value.value, data, len);

                esp_ble_gatts_send_response(gatts_if, param->read.conn_id,
                                            param->read.trans_id, ESP_GATT_OK,
                                            &rsp);
            }
            break;
        }

        case ESP_GATTS_WRITE_EVT: {
            ESP_LOGI(TAG, "Write request: handle=0x%04x, len=%d",
                     param->write.handle, param->write.len);

            if (param->write.handle == handle_table[IDX_SETTINGS_VAL]) {
                // Parse and apply settings
                charger_settings_t settings;
                if (settings_parse(param->write.value, param->write.len,
                                   &settings)) {
                    fsm_apply_settings(&settings);
                    ESP_LOGI(TAG, "Settings applied successfully");
                } else {
                    ESP_LOGW(TAG, "Invalid settings packet");
                }

                if (param->write.need_rsp) {
                    esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                                param->write.trans_id,
                                                ESP_GATT_OK, NULL);
                }
            } else if (param->write.handle == handle_table[IDX_CMD_VAL]) {
                // Process command
                if (param->write.len >= 1) {
                    fsm_process_command(
                        (charger_command_t)param->write.value[0]);
                }

                if (param->write.need_rsp) {
                    esp_ble_gatts_send_response(gatts_if, param->write.conn_id,
                                                param->write.trans_id,
                                                ESP_GATT_OK, NULL);
                }
            }
            break;
        }

        case ESP_GATTS_CONNECT_EVT:
            ESP_LOGI(TAG, "Client connected: conn_id=%d",
                     param->connect.conn_id);
            g_conn_id = param->connect.conn_id;
            g_is_connected = true;

            // Update connection parameters for iOS
            esp_ble_conn_update_params_t conn_params = {0};
            memcpy(conn_params.bda, param->connect.remote_bda,
                   sizeof(esp_bd_addr_t));
            conn_params.min_int = 0x10;
            conn_params.max_int = 0x20;
            conn_params.latency = 0;
            conn_params.timeout = 400;
            esp_ble_gap_update_conn_params(&conn_params);
            break;

        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(TAG, "Client disconnected");
            g_conn_id = 0xFFFF;
            g_is_connected = false;
            esp_ble_gap_start_advertising(&adv_params);
            break;

        case ESP_GATTS_MTU_EVT:
            ESP_LOGI(TAG, "MTU: %d", param->mtu.mtu);
            break;

        default:
            break;
    }
}

// ============================================================================
// Public Functions
// ============================================================================

void charger_gatt_init(void) {
    esp_err_t ret;

    // Release classic BT memory
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    // Initialize BT controller
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "BT controller init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "BT controller enable failed: %s", esp_err_to_name(ret));
        return;
    }

    // Initialize Bluedroid
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

    // Register callbacks
    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gap_register_callback(gap_event_handler);

    // Register app
    esp_ble_gatts_app_register(APP_ID);

    // Set MTU
    esp_ble_gatt_set_local_mtu(500);

    // Configure GPIOs for RF switch and antenna selection
    gpio_reset_pin(GPIO_NUM_3);
    gpio_set_direction(GPIO_NUM_3, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_3, 0);  // RF switch ON

    gpio_reset_pin(GPIO_NUM_14);
    gpio_set_direction(GPIO_NUM_14, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_14, 0);  // INTERNAL antenna

    ESP_LOGI(TAG, "GATT server initialized");
}

void charger_gatt_notify_status(void) {
    if (!g_is_connected || g_gatts_if == ESP_GATT_IF_NONE) {
        return;
    }

    // Check if notifications are enabled
    uint16_t ccc_val = 0;
    uint16_t len = sizeof(ccc_val);
    esp_ble_gatts_get_attr_value(handle_table[IDX_STATUS_CCC], &len,
                                 (const uint8_t**)&ccc_val);
    if (ccc_val == 0) {
        return;  // Notifications not enabled
    }

    // Build status packet
    charger_status_t status;
    fsm_get_status(&status);

    uint8_t data[STATUS_PACKET_SIZE];
    uint16_t data_len = status_serialize(&status, data, sizeof(data));

    // Send notification
    esp_ble_gatts_send_indicate(g_gatts_if, g_conn_id,
                                handle_table[IDX_STATUS_VAL], data_len, data,
                                false);  // false = notification, not indication
}
