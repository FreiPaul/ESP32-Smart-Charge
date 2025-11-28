/**
 * Charger Time Management Implementation
 */

#include "charger_time.h"

#include "esp_log.h"

static const char* TAG = "TIME";

// Current Unix timestamp
static uint32_t current_time = 0;

// Whether time has been synchronized
static bool time_synced = false;

void time_set(uint32_t unix_timestamp) {
    current_time = unix_timestamp;
    time_synced = true;
    ESP_LOGI(TAG, "Time synchronized: %lu", (unsigned long)unix_timestamp);
}

uint32_t time_get(void) {
    return current_time;
}

void time_tick(void) {
    if (time_synced) {
        current_time++;
    }
}

bool time_is_synced(void) {
    return time_synced;
}

void time_reset(void) {
    current_time = 0;
    time_synced = false;
    ESP_LOGI(TAG, "Time reset");
}
