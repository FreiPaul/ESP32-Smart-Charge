/**
 * Charger Hardware Abstraction
 *
 * Provides interface for:
 * - Relay control (GPIO 2)
 * - Voltage reading (ADC1 CH0 / GPIO 0)
 */

#ifndef CHARGER_HARDWARE_H
#define CHARGER_HARDWARE_H

#include <stdbool.h>
#include <stdint.h>

// Hardware configuration
#define RELAY_GPIO 2
#define INVERT_RELAY_LOGIC 1  // Set to 1 if relay logic is inverted
#define VOLTAGE_ADC_GPIO 0

// Voltage divider ratio: actual_voltage = adc_voltage * VOLTAGE_DIVIDER_RATIO
// Adjust this based on your voltage divider resistor values
// Example: For 100k/25k divider measuring 0-16.5V: ratio = 5.0
#define VOLTAGE_DIVIDER_RATIO 5.0f

// ADC configuration
#define ADC_ATTEN ADC_ATTEN_DB_12  // 0-3.3V range
#define ADC_WIDTH ADC_BITWIDTH_12  // 12-bit resolution (0-4095)

/**
 * Initialize hardware (GPIO for relay, ADC for voltage)
 */
void hardware_init(void);

/**
 * Set relay state
 *
 * @param on true to turn relay ON (charging), false for OFF
 */
void relay_set(bool on);

/**
 * Get current relay state
 *
 * @return true if relay is ON, false if OFF
 */
bool relay_get_state(void);

/**
 * Read voltage from ADC
 *
 * Applies voltage divider ratio to calculate actual voltage.
 *
 * @return Voltage in millivolts
 */
uint16_t voltage_read_mv(void);

#endif  // CHARGER_HARDWARE_H
