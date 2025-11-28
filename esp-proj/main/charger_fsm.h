/**
 * Charger Finite State Machine
 *
 * States:
 * - IDLE: No schedule, relay OFF
 * - SCHEDULED: Waiting for start time
 * - CHARGING: Relay ON, actively charging
 * - THRESHOLD_DELAY: Voltage threshold reached, 10s countdown
 * - COMPLETE: Charging finished
 * - ERROR: Error condition
 */

#ifndef CHARGER_FSM_H
#define CHARGER_FSM_H

#include "charger_settings.h"

// Threshold delay before switching off (seconds)
#define THRESHOLD_DELAY_SECONDS 10

/**
 * FSM States
 */
typedef enum {
    CHARGER_STATE_IDLE = 0,
    CHARGER_STATE_SCHEDULED = 1,
    CHARGER_STATE_CHARGING = 2,
    CHARGER_STATE_THRESHOLD_DELAY = 3,
    CHARGER_STATE_COMPLETE = 4,
    CHARGER_STATE_ERROR = 5,
} charger_state_t;

/**
 * Error codes
 */
typedef enum {
    ERROR_NONE = 0,
    ERROR_INVALID_SETTINGS = 1,
    ERROR_HARDWARE_FAULT = 2,
} charger_error_t;

/**
 * Initialize FSM
 *
 * Sets initial state to IDLE.
 */
void fsm_init(void);

/**
 * Apply new settings from BLE
 *
 * Parses settings and transitions state accordingly:
 * - If schedule enabled and start time in future: → SCHEDULED
 * - If schedule enabled and start time in past: → CHARGING
 * - If no schedule: → IDLE
 *
 * @param settings Parsed settings structure
 */
void fsm_apply_settings(const charger_settings_t* settings);

/**
 * Process command from BLE
 *
 * @param cmd Command code (CMD_FORCE_START, CMD_FORCE_STOP, CMD_CLEAR_SCHEDULE)
 */
void fsm_process_command(charger_command_t cmd);

/**
 * Reconcile state (called every 1 second)
 *
 * Checks current time and voltage, transitions state as needed:
 * - SCHEDULED: check if start time reached
 * - CHARGING: check if voltage threshold exceeded
 * - THRESHOLD_DELAY: countdown and check for voltage drop
 */
void fsm_reconcile(void);

/**
 * Get current status for BLE read/notify
 *
 * @param status Output status structure
 */
void fsm_get_status(charger_status_t* status);

/**
 * Get current FSM state
 *
 * @return Current state
 */
charger_state_t fsm_get_state(void);

#endif  // CHARGER_FSM_H
