#pragma once

#include <Arduino.h>
#include "espnow_link.h"
#include "config.h"

// ============================================================================
// Receiver State Structure
// ============================================================================
struct RxChannelState {
  uint16_t channels[RX_NUM_CHANNELS]; // CH1 to CH8 pulse widths in microseconds (1000 - 2000 µs)
  uint32_t last_packet_time;          // millis() timestamp of last verified packet
  uint32_t packets_received;          // Total valid packets received
  uint16_t pps;                       // Valid packets-per-second received
  bool     failsafe;                  // true if signal lost (>500ms without packet)
  uint8_t  transmitter_mac[6];        // Auto-discovered MAC address of transmitter
  bool     transmitter_paired;        // true once transmitter MAC is captured
};

extern RxChannelState rx_state;

// Direct variable aliases for the PWM channels:
extern const uint16_t &rx_ch1_roll;
extern const uint16_t &rx_ch2_pitch;
extern const uint16_t &rx_ch3_throttle;
extern const uint16_t &rx_ch4_yaw;
extern const uint16_t &rx_ch5_aux1;
extern const uint16_t &rx_ch6_aux2;
extern const uint16_t &rx_ch7_aux3;
extern const uint16_t &rx_ch8_aux4;
extern const uint16_t &rx_ch9_aux5; // AUX 5 PWM Pulse (1000 - 2000 µs on GPIO 18)
extern const uint8_t  &rx_raw_aux5; // AUX 5 raw 6-bit value (0 - 63)
extern const bool     &rx_failsafe_active;


// ============================================================================
// Public API Functions
// ============================================================================

/**
 * Initializes the 8-channel 12-bit @ 50Hz LEDC PWM outputs,
 * configures Wi-Fi in Long Range (LR) mode, and registers the ESP-NOW receiver.
 * 
 * @return true if initialized successfully.
 */
bool rx_pwm_init();

/**
 * Handles failsafe watchdog and periodic telemetry uplink back to the transmitter.
 * Call this in loop() when running in Receiver mode.
 */
void rx_pwm_update();

/**
 * Directly writes a pulse width in microseconds (1000 to 2000 µs) to a channel (0 to 7).
 * Maps 1000 - 2000 µs to 12-bit LEDC duty cycle at 50 Hz.
 * 
 * @param channel_idx Channel index (0 to 7).
 * @param pulse_us    Pulse width in microseconds (1000 - 2000 µs).
 */
void rx_pwm_write_channel(uint8_t channel_idx, uint16_t pulse_us);

/**
 * Reads battery voltage from the configured ADC pin (or returns simulated 3850 mV).
 */
uint16_t rx_read_battery_mv();
