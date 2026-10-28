#pragma once

#include <Arduino.h>

// ============================================================================
// DEVICE ROLE SELECTION
// ============================================================================
// You can set the role in two ways:
//   1. Via PlatformIO environment: select 'seeed-xiao-esp32-c6' for TX,
//      or 'rx-esp32dev' for RX.
//   2. Or manually toggle the #define below:
#if !defined(ROLE_TRANSMITTER) && !defined(ROLE_RECEIVER)
  #define ROLE_TRANSMITTER  // <-- Comment out and uncomment ROLE_RECEIVER for RX
  //#define ROLE_RECEIVER
#endif

// ============================================================================
// RECEIVER (RX) CONFIGURATION - ESP32 Dev Module
// ============================================================================
#define RX_PWM_FREQ       50    // 50 Hz PWM frequency for servos / ESCs (20ms period)
#define RX_PWM_RES        12    // 12-bit resolution (0 to 4095 duty count)
#define RX_NUM_CHANNELS   9     // 9 output channels (4 Flight + 5 AUX)

// Output GPIOs on ESP32 Dev Module for all 9 channels:
//   CH1: Roll     (GPIO 13)
//   CH2: Pitch    (GPIO 14)
//   CH3: Throttle (GPIO 27)
//   CH4: Yaw      (GPIO 26)
//   CH5: AUX 1    (GPIO 25) - Arm Switch (Bit 0)
//   CH6: AUX 2    (GPIO 33) - Mode Switch (Bits 1..3)
//   CH7: AUX 3    (GPIO 32) - Switch/Dial (Bits 4..6)
//   CH8: AUX 4    (GPIO 4)  - Switch/Dial (Bits 7..9)
//   CH9: AUX 5    (GPIO 18) - Auxiliary Channel 5 (Bits 10..15)
static const uint8_t RX_PWM_PINS[RX_NUM_CHANNELS] = {
  13, // CH1: Roll
  14, // CH2: Pitch
  27,  // CH3: Throttle
  26, // CH4: Yaw
  25, // CH5: AUX 1 (Arm)
  33, // CH6: AUX 2
  32, // CH7: AUX 3
  4,  // CH8: AUX 4
  18  // CH9: AUX 5
};

// Battery ADC Monitoring pin on ESP32 (GPIO 34 is typical, or set to 255 if unused)
#define RX_BATTERY_PIN         34
#define RX_BATTERY_DIVIDER     4.65f // Standard voltage divider ratio (e.g. 10k / 2.7k)

// RX Failsafe timeout in milliseconds
#define RX_FAILSAFE_TIMEOUT_MS 500
