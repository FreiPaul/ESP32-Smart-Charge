/**
 * Charger GATT Server
 *
 * BLE GATT service with characteristics:
 * - Settings (Write): Configure charger
 * - Status (Read/Notify): Get current state
 * - Command (Write): Immediate actions
 */

#ifndef CHARGER_GATT_H
#define CHARGER_GATT_H

#include <stdint.h>

// Device name for advertising
#define DEVICE_NAME "ESP-CHARGER"

// Application ID
#define APP_ID 0x55

/**
 * Initialize GATT server and start advertising
 */
void charger_gatt_init(void);

/**
 * Send status notification to connected client
 *
 * Called by reconcile task to push status updates.
 */
void charger_gatt_notify_status(void);

#endif  // CHARGER_GATT_H
