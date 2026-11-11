#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "usb_comm.h"
#include "afh_link.h"

// ============================================================================
// Configuration & Timing Constants
// ============================================================================

// Strict 50Hz link rate (20ms interval per transmission)
#define ESPNOW_SEND_INTERVAL_MS       20

// Telemetry loss timeout (1200ms without packet marks connection lost)
#define ESPNOW_DISCONNECT_TIMEOUT_MS  1200

// Default peer MAC address: 80:B5:4E:C1:10:EC
extern const uint8_t DEFAULT_DRONE_MAC[6];

// ============================================================================
// Protocol Data Structures (Packed, Little-Endian)
// ============================================================================
#pragma pack(push, 1)

/**
 * 22-byte Downlink Flight Control Packet sent to the drone at 50Hz:
 *  - 4-byte sequence ID (packet_id)
 *  - 4 channels (2 bytes each = 8 bytes): Roll, Pitch, Yaw, Throttle (1000 - 2000 µs)
 *  - 16-bit auxiliary channels bitfield (2 bytes): aux_channels
 *  - 6-byte AFH synchronization header:
 *      * 2-byte hop epoch
 *      * 1-byte current channel
 *      * 1-byte next channel
 *      * 2-byte active channel bitmask
 *  - 2-byte CRC-16 checksum (crc) over all preceding 20 bytes
 */
typedef struct {
  uint32_t     packet_id;    // 4 bytes: Incrementing sequence counter
  uint16_t     roll;         // 2 bytes: Channel 1 (1000 - 2000 µs, 1500 = Center)
  uint16_t     pitch;        // 2 bytes: Channel 2 (1000 - 2000 µs, 1500 = Center)
  uint16_t     yaw;          // 2 bytes: Channel 3 (1000 - 2000 µs, 1500 = Center)
  uint16_t     throttle;     // 2 bytes: Channel 4 (1000 - 2000 µs, 1000 = Cutoff)
  uint16_t     aux_channels; // 2 bytes: 16-bit Auxiliary bitfield (Bit 0 = Arm)
  afh_header_t afh;          // 6 bytes: Adaptive Frequency Hopping sync header
  uint16_t     crc;          // 2 bytes: CRC-16 checksum of all preceding fields
} flight_control_packet_t;

/**
 * 7-byte Uplink Telemetry Packet received from the drone:
 * Matches ESPNOW_TX_telemetry & ESPNOW_Rx_telemetry specification + AFH metrics.
 */
typedef struct {
  uint8_t  packet_id;          // Telemetry packet ID
  uint16_t packet_count_500ms; // Packets received by drone in previous 500ms window
  uint16_t battery_mV;         // Drone battery voltage in millivolts
  uint8_t  rx_channel;         // Radio channel RX is currently tuned to
  uint8_t  rx_rssi;            // Reserved link quality / RSSI indicator
} telemetry_packet_t;

#pragma pack(pop)

// ============================================================================
// Real-time Drone Telemetry State
// ============================================================================
struct DroneTelemetry {
  uint16_t battery_mV;        // Battery in millivolts (e.g. 3850 mV)
  float    battery_v;         // Battery in volts (e.g. 3.85 V)
  uint16_t pps;               // Packets-per-second (packet_count_500ms * 2)
  uint32_t last_packet_time;  // Timestamp in millis() of last received telemetry
  bool     is_connected;      // true if telemetry packet was received within 1200ms
};

// Primary telemetry state instance
extern DroneTelemetry drone_telemetry;

// Direct variable aliases for convenience
extern const uint16_t &drone_battery_mv;
extern const float    &drone_battery_v;
extern const uint16_t &drone_pps;
extern const bool     &drone_connected;

// Current outgoing channel state aliases
extern const uint16_t &espnow_tx_roll;
extern const uint16_t &espnow_tx_pitch;
extern const uint16_t &espnow_tx_yaw;
extern const uint16_t &espnow_tx_throttle;
extern const uint16_t &espnow_tx_aux;

// ============================================================================
// Public API Functions
// ============================================================================

/**
 * Calculates the standard CRC-16 over a flight_control_packet_t (all bytes except crc).
 */
uint16_t espnow_calculate_crc16(const flight_control_packet_t &packet);

/**
 * Verifies if the packet's CRC field matches the calculated CRC-16.
 */
bool espnow_verify_crc16(const flight_control_packet_t &packet);

/**
 * Initializes WiFi in Long Range (LR) station mode, registers ESP-NOW callbacks,
 * binds the peer drone MAC, and launches the FreeRTOS 50Hz transmission task.
 * 
 * @param peer_mac 6-byte peer MAC address (pass nullptr to use default 80:B5:4E:C1:10:EC).
 * @return true if initialization was successful.
 */
bool espnow_link_init(const uint8_t *peer_mac = nullptr);

/**
 * Manually set the 4 RC channels and 16-bit aux channel (all 2-byte values).
 * 
 * @param roll         Channel 1 (1000 - 2000 µs)
 * @param pitch        Channel 2 (1000 - 2000 µs)
 * @param yaw          Channel 3 (1000 - 2000 µs)
 * @param throttle     Channel 4 (1000 - 2000 µs)
 * @param aux_channels 16-bit aux bitfield (Bit 0 = Arm)
 */
void espnow_link_set_channels(uint16_t roll, uint16_t pitch, uint16_t yaw, uint16_t throttle, uint16_t aux_channels);

/**
 * Copies the raw 2-byte channels directly from UserInput (roll, pitch, yaw, throttle, aux_channels).
 * Automatically applies failsafe cutoff if USB is disconnected or disarmed.
 * 
 * @param input The current user input state.
 */
void espnow_link_feed_usb_input(const UserInput &input);

/**
 * Enables or disables automatic feeding from usb_input inside the 50Hz RTOS task.
 * Default is TRUE.
 */
void espnow_link_enable_auto_feed(bool enable);

/**
 * Enables or disables automatic forwarding of incoming drone telemetry to the mobile app
 * over USB-CDC using usb_comm_send_telemetry().
 * Default is TRUE.
 */
void espnow_link_enable_usb_forward(bool enable);

/**
 * Change the peer drone MAC address dynamically.
 */
bool espnow_link_set_peer(const uint8_t *peer_mac);
