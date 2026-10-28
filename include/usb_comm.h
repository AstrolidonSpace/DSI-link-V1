#pragma once

#include <Arduino.h>

// ============================================================================
// Control & Failsafe Parameters
// ============================================================================
#define THROTTLE_MIN        1000  // Cutoff / Idle throttle in microseconds
#define THROTTLE_MAX        2000  // Full throttle in microseconds
#define FAILSAFE_TIMEOUT_MS 250   // Maximum allowable packet loss (ms) before failsafe kicks in

// ============================================================================
// User Input Structure
// ============================================================================
/**
 * Holds all real-time control setpoints and communication link status
 * decoded from the USB interface.
 */
struct UserInput {
  uint16_t roll;             // 1000 - 2000 µs (1500 = Neutral Center)
  uint16_t pitch;            // 1000 - 2000 µs (1500 = Neutral Center)
  uint16_t yaw;              // 1000 - 2000 µs (1500 = Neutral Center)
  uint16_t throttle;         // 1000 - 2000 µs (1000 = Cutoff / Idle)
  uint16_t aux_channels;     // 16-bit bitfield (Bit 0 = Primary Arming toggle)
  bool     is_armed;         // Convenience flag: true when aux_channels bit 0 is set and link is active
  bool     is_connected;     // true when valid packets are being received within FAILSAFE_TIMEOUT_MS
  bool     failsafe;         // true when connection is lost (>250 ms without valid packet)
  bool     new_data;         // true for the loop cycle in which a new packet was received
  uint32_t last_packet_time;  // Timestamp of the last valid packet in millis()
  uint8_t  pps;              // Live link quality: Packets-Per-Second received
};

// ============================================================================
// Direct Access Variables
// ============================================================================

// 1. Primary structure holding all user inputs
extern UserInput usb_input;

// 2. Direct individual variable aliases for maximum convenience:
//    You can use these directly in your code (e.g. `usb_throttle`, `usb_roll`)
extern const uint16_t &usb_throttle;
extern const uint16_t &usb_roll;
extern const uint16_t &usb_pitch;
extern const uint16_t &usb_yaw;
extern const uint16_t &usb_aux;
extern const bool     &usb_armed;
extern const bool     &usb_connected;

// ============================================================================
// Function Declarations
// ============================================================================

/**
 * Initializes the USB-CDC Serial interface and default state values.
 * Call this in setup().
 * 
 * @param baud Baud rate (default: 115200).
 */
void usb_comm_init(uint32_t baud = 115200);

/**
 * Reads incoming USB-CDC serial stream, decodes COBS frames, checks CRC16,
 * updates user input variables, handles failsafe timeouts, and updates PPS metrics.
 * Call this in loop().
 * 
 * @return true if a new valid control packet was received during this cycle.
 */
bool usb_comm_update();

/**
 * Optionally configure telemetry values (e.g. from real ADC / battery sensors).
 * If left at 0, default simulated values (3.85V, current mapped to throttle) are sent.
 * 
 * @param voltage_mv Battery voltage in millivolts.
 * @param current_ma Current draw in milliamperes.
 */
void usb_comm_set_telemetry(uint16_t voltage_mv, uint16_t current_ma);

/**
 * Constructs and transmits an ICD 0x51 CRTP telemetry frame over USB.
 * This is called automatically upon receiving each valid downlink command,
 * but can also be triggered manually if desired.
 */
void usb_comm_send_telemetry();
