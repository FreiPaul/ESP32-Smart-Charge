/**
 * Charger Finite State Machine Implementation
 */

#include "charger_fsm.h"

#include <string.h>

#include "charger_hardware.h"
#include "charger_time.h"
#include "esp_log.h"

static const char* TAG = "FSM";

// Current state
static charger_state_t g_state = CHARGER_STATE_IDLE;

// Current settings
static charger_settings_t g_settings = {0};

// Threshold countdown timer
static uint8_t g_threshold_countdown = 0;

// Error code
static charger_error_t g_error = ERROR_NONE;

// State name strings for logging
static const char* state_names[] = {
    "IDLE", "SCHEDULED", "CHARGING", "DELAY", "COMPLETE", "ERROR",
};

static void transition_to(charger_state_t new_state) {
    if (g_state != new_state) {
        ESP_LOGI(TAG, "State: %s -> %s", state_names[g_state],
                 state_names[new_state]);
        g_state = new_state;
    }
}

void fsm_init(void) {
    g_state = CHARGER_STATE_IDLE;
    memset(&g_settings, 0, sizeof(g_settings));
    g_threshold_countdown = 0;
    g_error = ERROR_NONE;
    relay_set(false);
    ESP_LOGI(TAG, "FSM initialized");
}

void fsm_apply_settings(const charger_settings_t* settings) {
    // Store settings
    memcpy(&g_settings, settings, sizeof(charger_settings_t));

    // Synchronize time
    time_set(settings->current_time);

    ESP_LOGI(TAG, "Settings applied: flags=0x%02x, threshold=%umV, start=%lu",
             settings->flags, settings->voltage_threshold_mv,
             (unsigned long)settings->scheduled_start_time);

    // Clear any previous error
    g_error = ERROR_NONE;
    g_threshold_countdown = 0;

    // Determine new state based on settings
    if (settings->flags & FLAG_SCHEDULE_ENABLED) {
        uint32_t now = time_get();
        if (settings->scheduled_start_time <= now) {
            // Start time is in the past or now - start charging immediately
            ESP_LOGI(TAG, "Start time reached, starting charge");
            relay_set(true);
            transition_to(CHARGER_STATE_CHARGING);
        } else {
            // Start time is in the future - wait
            ESP_LOGI(TAG, "Scheduled for %lu (in %lu seconds)",
                     (unsigned long)settings->scheduled_start_time,
                     (unsigned long)(settings->scheduled_start_time - now));
            relay_set(false);
            transition_to(CHARGER_STATE_SCHEDULED);
        }
    } else {
        // No schedule - go to idle
        relay_set(false);
        transition_to(CHARGER_STATE_IDLE);
    }
}

void fsm_process_command(charger_command_t cmd) {
    switch (cmd) {
        case CMD_FORCE_START:
            ESP_LOGI(TAG, "Command: Force start");
            relay_set(true);
            g_threshold_countdown = 0;
            transition_to(CHARGER_STATE_CHARGING);
            break;

        case CMD_FORCE_STOP:
            ESP_LOGI(TAG, "Command: Force stop");
            relay_set(false);
            g_threshold_countdown = 0;
            g_settings.flags &= ~FLAG_SCHEDULE_ENABLED;  // Clear schedule
            transition_to(CHARGER_STATE_IDLE);
            break;

        case CMD_CLEAR_SCHEDULE:
            ESP_LOGI(TAG, "Command: Clear schedule");
            g_settings.flags &= ~FLAG_SCHEDULE_ENABLED;
            g_threshold_countdown = 0;
            if (g_state == CHARGER_STATE_SCHEDULED) {
                transition_to(CHARGER_STATE_IDLE);
            }
            break;

        default:
            ESP_LOGW(TAG, "Unknown command: 0x%02x", cmd);
            break;
    }
}

void fsm_reconcile(void) {
    uint32_t current_time = time_get();
    uint16_t voltage = voltage_read_mv();

    switch (g_state) {
        case CHARGER_STATE_IDLE:
            // Nothing to do in idle state
            break;

        case CHARGER_STATE_SCHEDULED:
            // Check if start time reached
            if (time_is_synced() &&
                current_time >= g_settings.scheduled_start_time) {
                ESP_LOGI(TAG, "Schedule triggered at time %lu",
                         (unsigned long)current_time);
                relay_set(true);
                transition_to(CHARGER_STATE_CHARGING);
            }
            break;

        case CHARGER_STATE_CHARGING:
            // Check if voltage threshold exceeded (if enabled)
            if ((g_settings.flags & FLAG_VOLTAGE_THRESHOLD_ENABLED) &&
                voltage >= g_settings.voltage_threshold_mv) {
                ESP_LOGI(TAG, "Voltage threshold reached: %umV >= %umV",
                         voltage, g_settings.voltage_threshold_mv);
                g_threshold_countdown = THRESHOLD_DELAY_SECONDS;
                transition_to(CHARGER_STATE_THRESHOLD_DELAY);
            }
            break;

        case CHARGER_STATE_THRESHOLD_DELAY:
            // Check if voltage dropped (no-load spike, false alarm)
            if (voltage < g_settings.voltage_threshold_mv) {
                ESP_LOGI(TAG, "Voltage dropped to %umV, continuing charge",
                         voltage);
                g_threshold_countdown = 0;
                transition_to(CHARGER_STATE_CHARGING);
            } else {
                // Countdown
                g_threshold_countdown--;
                ESP_LOGI(TAG, "Threshold delay: %u seconds remaining",
                         g_threshold_countdown);

                if (g_threshold_countdown == 0) {
                    // Countdown finished, stop charging
                    ESP_LOGI(TAG, "Charging complete, voltage stable at %umV",
                             voltage);
                    relay_set(false);
                    g_settings.flags &=
                        ~FLAG_SCHEDULE_ENABLED;  // Clear schedule
                    transition_to(CHARGER_STATE_COMPLETE);
                }
            }
            break;

        case CHARGER_STATE_COMPLETE:
            // Stay in complete state until new settings received
            break;

        case CHARGER_STATE_ERROR:
            // Stay in error state until reset
            break;
    }
}

void fsm_get_status(charger_status_t* status) {
    status->version = SETTINGS_VERSION;
    status->state = (uint8_t)g_state;
    status->voltage_mv = voltage_read_mv();
    status->device_time = time_get();
    status->relay_state = relay_get_state() ? 1 : 0;
    status->threshold_countdown = g_threshold_countdown;
    status->error_code = (uint8_t)g_error;
    status->scheduled_start_time = g_settings.scheduled_start_time;
    status->voltage_threshold_mv = g_settings.voltage_threshold_mv;
    status->flags = g_settings.flags;
}

charger_state_t fsm_get_state(void) { return g_state; }

// Settings parsing implementation
bool settings_parse(const uint8_t* data, uint16_t len,
                    charger_settings_t* out) {
    if (len < SETTINGS_PACKET_SIZE) {
        ESP_LOGW(TAG, "Settings packet too short: %u < %u", len,
                 SETTINGS_PACKET_SIZE);
        return false;
    }

    // Verify checksum
    uint8_t checksum = 0;
    for (int i = 0; i < SETTINGS_PACKET_SIZE - 1; i++) {
        checksum ^= data[i];
    }
    if (checksum != data[SETTINGS_PACKET_SIZE - 1]) {
        ESP_LOGW(TAG,
                 "Settings checksum mismatch: calculated 0x%02x, got 0x%02x",
                 checksum, data[SETTINGS_PACKET_SIZE - 1]);
        return false;
    }

    // Verify version
    if (data[0] != SETTINGS_VERSION) {
        ESP_LOGW(TAG, "Settings version mismatch: expected 0x%02x, got 0x%02x",
                 SETTINGS_VERSION, data[0]);
        return false;
    }

    // Parse fields (little-endian)
    out->version = data[0];
    out->current_time =
        data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);
    out->flags = data[5];
    out->scheduled_start_time =
        data[6] | (data[7] << 8) | (data[8] << 16) | (data[9] << 24);
    out->voltage_threshold_mv = data[10] | (data[11] << 8);

    return true;
}

// Status serialization implementation
uint16_t status_serialize(const charger_status_t* status, uint8_t* out,
                          uint16_t max_len) {
    if (max_len < STATUS_PACKET_SIZE) {
        return 0;
    }

    out[0] = status->version;
    out[1] = status->state;
    out[2] = status->voltage_mv & 0xFF;
    out[3] = (status->voltage_mv >> 8) & 0xFF;
    out[4] = status->device_time & 0xFF;
    out[5] = (status->device_time >> 8) & 0xFF;
    out[6] = (status->device_time >> 16) & 0xFF;
    out[7] = (status->device_time >> 24) & 0xFF;
    out[8] = status->relay_state;
    out[9] = status->threshold_countdown;
    out[10] = status->error_code;
    out[11] = status->scheduled_start_time & 0xFF;
    out[12] = (status->scheduled_start_time >> 8) & 0xFF;
    out[13] = (status->scheduled_start_time >> 16) & 0xFF;
    out[14] = (status->scheduled_start_time >> 24) & 0xFF;
    out[15] = status->voltage_threshold_mv & 0xFF;
    out[16] = (status->voltage_threshold_mv >> 8) & 0xFF;
    out[17] = status->flags;
    return STATUS_PACKET_SIZE;
}
