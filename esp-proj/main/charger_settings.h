/**
 * Charger Settings Data Structures
 *
 * Defines the binary protocol for BLE communication:
 * - Settings packet (written by app)
 * - Status packet (read/notified to app)
 * - Command codes
 */

#ifndef CHARGER_SETTINGS_H
#define CHARGER_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

// Protocol version
#define SETTINGS_VERSION 0x01

// Settings flags (byte 5 of settings packet)
#define FLAG_VOLTAGE_THRESHOLD_ENABLED (1 << 0)
#define FLAG_SCHEDULE_ENABLED (1 << 1)

// Packet sizes
#define SETTINGS_PACKET_SIZE 14
#define STATUS_PACKET_SIZE 12

// Command codes
typedef enum {
    CMD_FORCE_START = 0x01,
    CMD_FORCE_STOP = 0x02,
    CMD_CLEAR_SCHEDULE = 0x03,
} charger_command_t;

/**
 * Settings structure (received from app)
 *
 * BLE Packet format (14 bytes):
 * Byte 0:     Version (0x01)
 * Byte 1-4:   Current Unix timestamp (uint32 LE)
 * Byte 5:     Flags (bit0=threshold_enabled, bit1=schedule_enabled)
 * Byte 6-9:   Scheduled start time (uint32 LE)
 * Byte 10-11: Voltage threshold (millivolts, uint16 LE)
 * Byte 12:    Reserved
 * Byte 13:    Checksum (XOR of bytes 0-12)
 */
typedef struct {
    uint8_t version;
    uint32_t current_time;          // Unix timestamp from phone
    uint8_t flags;
    uint32_t scheduled_start_time;  // Unix timestamp for start
    uint16_t voltage_threshold_mv;  // Millivolts
} charger_settings_t;

/**
 * Status structure (sent to app)
 *
 * BLE Packet format (12 bytes):
 * Byte 0:   Version (0x01)
 * Byte 1:   FSM state
 * Byte 2-3: Current voltage (millivolts, uint16 LE)
 * Byte 4-7: Device time (uint32 LE)
 * Byte 8:   Relay state (0=off, 1=on)
 * Byte 9:   Threshold countdown (0-10 seconds, 0=inactive)
 * Byte 10:  Error code (0=none)
 * Byte 11:  Reserved
 */
typedef struct {
    uint8_t version;
    uint8_t state;
    uint16_t voltage_mv;
    uint32_t device_time;
    uint8_t relay_state;
    uint8_t threshold_countdown;
    uint8_t error_code;
    uint8_t reserved;
} charger_status_t;

/**
 * Parse settings from BLE packet
 *
 * @param data Raw BLE packet data
 * @param len Length of data
 * @param out Parsed settings structure
 * @return true if parsing successful, false if invalid data
 */
bool settings_parse(const uint8_t* data, uint16_t len, charger_settings_t* out);

/**
 * Serialize status to BLE packet
 *
 * @param status Status structure to serialize
 * @param out Output buffer (must be at least STATUS_PACKET_SIZE bytes)
 * @param max_len Maximum output buffer size
 * @return Number of bytes written
 */
uint16_t status_serialize(const charger_status_t* status, uint8_t* out,
                          uint16_t max_len);

#endif  // CHARGER_SETTINGS_H
