/**
 * Charger Time Management
 *
 * Maintains internal Unix timestamp by:
 * - Receiving initial time from phone via BLE
 * - Incrementing by 1 each second via reconcile task
 */

#ifndef CHARGER_TIME_H
#define CHARGER_TIME_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Set current time (called when receiving timestamp from phone)
 *
 * @param unix_timestamp Unix timestamp (seconds since 1970-01-01)
 */
void time_set(uint32_t unix_timestamp);

/**
 * Get current time
 *
 * @return Current Unix timestamp (0 if not synced)
 */
uint32_t time_get(void);

/**
 * Increment time by 1 second
 *
 * Called by reconcile task every second.
 */
void time_tick(void);

/**
 * Check if time has been synchronized
 *
 * @return true if time_set() has been called, false otherwise
 */
bool time_is_synced(void);

/**
 * Reset time synchronization
 *
 * Called when settings are cleared or on error.
 */
void time_reset(void);

#endif  // CHARGER_TIME_H
