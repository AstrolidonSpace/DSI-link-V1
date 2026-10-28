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
// USB Communication Functions
// ============================================================================

/**
 * Initializes the USB-CDC Serial interface and internal synchronization primitives.
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
 * Transmits an ICD 0x51 CRTP telemetry frame over USB to the mobile app.
 * Call this function whenever telemetry data arrives from your RX ESP32.
 * 
 * Collision Prevention & Performance:
 *  - Full-Duplex Safe: USB CDC hardware uses separate IN/OUT endpoints, so sending
 *    telemetry will NEVER corrupt or collide with incoming control commands.
 *  - Mutex-Protected: Thread-safe if called from FreeRTOS tasks, ISR contexts, or UART callbacks.
 *  - Atomic: The full COBS packet and delimiter are written together and flushed immediately.
 * 
 * @param voltage_mv Battery voltage in millivolts (e.g. 3850 for 3.85V, 7400 for 7.4V).
 * @param current_ma Current draw in milliamperes (e.g. 1500 for 1.5A).
 * @param pps        Optional: Link quality / Packets-Per-Second (0 to 255).
 *                   Defaults to -1 to automatically use the live calculated link PPS.
 * @return true if the frame was encoded and pushed over USB successfully.
 */
bool usb_comm_send_telemetry(uint16_t voltage_mv, uint16_t current_ma, int16_t pps = -1);

/**
 * Convenience overload: sends telemetry using default simulated or preset values.
 * 
 * @return true if sent successfully.
 */
bool usb_comm_send_telemetry();

/**
 * Enables or disables automatic telemetry echo on incoming command packets.
 * Default is FALSE (disabled) so that you have 100% control over when telemetry is sent.
 * 
 * @param enable Set to true to automatically echo telemetry upon receiving every command.
 */
void usb_comm_enable_auto_telemetry(bool enable);

/**
 * Optionally configure default fallback telemetry values.
 * 
 * @param voltage_mv Battery voltage in millivolts.
 * @param current_ma Current draw in milliamperes.
 */
void usb_comm_set_telemetry(uint16_t voltage_mv, uint16_t current_ma);
