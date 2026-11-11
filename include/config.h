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
// TRANSMITTER (TX) OPERATION MODE SELECTION
// ============================================================================
// When running as TRANSMITTER, choose operation mode:
//   TX_MODE_TRANSMIT  (1) : Normal 50Hz ESP-NOW Long Range transmission to drone receiver.
//   TX_MODE_WEB_DEBUG (2) : Wi-Fi Debug Web Page mode (AP: DSI-LINK-DEBUG, 192.168.4.1).
//                           Hosts a live web dashboard to inspect all 9 channel values,
//                           connection status, PPS, and USB packet statistics.
#define TX_MODE_TRANSMIT   1
#define TX_MODE_WEB_DEBUG  2

#ifndef TX_OPERATION_MODE
  //#define TX_OPERATION_MODE  TX_MODE_WEB_DEBUG   // <-- Active: Web Debug Page (http://192.168.4.1)
  #define TX_OPERATION_MODE  TX_MODE_TRANSMIT  // <-- Uncomment for normal 50Hz ESP-NOW transmission
#endif

// Wi-Fi SoftAP credentials for Web Debug mode
#define TX_WEB_SSID        "DSI-LINK-DEBUG"
#define TX_WEB_PASSWORD    "12345678"

// ============================================================================
// ADAPTIVE FREQUENCY HOPPING (AFH) CONFIGURATION
// ============================================================================
#define AFH_ENABLED            1    // 1 = Enable Adaptive Slow Frequency Hopping (10 hops/s)
#define AFH_INITIAL_CHANNEL    1    // Default boot channel (1 - 13)

// ============================================================================
// RECEIVER (RX) CONFIGURATION (ESP32 Dev Module & ESP32-S3)
// ============================================================================
#define RX_PWM_FREQ       50    // 50 Hz PWM frequency for servos / ESCs (20ms period)
#define RX_PWM_RES        12    // 12-bit resolution (0 to 4095 duty count)
#define RX_NUM_CHANNELS   9     // 9 output channels (4 Flight + 5 AUX)

#if defined(CONFIG_IDF_TARGET_ESP32S3)
// ----------------------------------------------------------------------------
// RECEIVER (RX) PIN CONFIGURATION - ESP32-S3 (DevKitC-1 / XIAO ESP32-S3)
// ----------------------------------------------------------------------------
// Output GPIOs for 9 PWM channels (avoiding USB pins 19/20 and Flash/PSRAM 26-32):
//   CH1: Roll     (GPIO 4)
//   CH2: Pitch    (GPIO 5)
//   CH3: Throttle (GPIO 6)
//   CH4: Yaw      (GPIO 7)
//   CH5: AUX 1    (GPIO 15) - Arm Switch (Bit 0)
//   CH6: AUX 2    (GPIO 16) - Mode Switch (Bits 1..3)
//   CH7: AUX 3    (GPIO 17) - Switch/Dial (Bits 4..6)
//   CH8: AUX 4    (GPIO 18) - Switch/Dial (Bits 7..9)
//   CH9: AUX 5    (GPIO 8)  - Auxiliary Channel 5 (Bits 10..15)
static const uint8_t RX_PWM_PINS[RX_NUM_CHANNELS] = {
  4,  // CH1: Roll
  5,  // CH2: Pitch
  6,  // CH3: Throttle
  7,  // CH4: Yaw
  15, // CH5: AUX 1 (Arm)
  16, // CH6: AUX 2
  17, // CH7: AUX 3
  18, // CH8: AUX 4
  8   // CH9: AUX 5
};

// Battery ADC Monitoring pin on ESP32-S3 (GPIO 1 is ADC1 Channel 0)
#define RX_BATTERY_PIN         1
#define RX_BATTERY_DIVIDER     4.65f // Standard voltage divider ratio (e.g. 10k / 2.7k)

// Connection Status Indicator LED (GPIO 21 on XIAO ESP32-S3, or GPIO 2 on DevKitC-1)
#if defined(ARDUINO_XIAO_ESP32S3) || defined(SEEED_XIAO_ESP32S3)
  #define RX_STATUS_PIN        21
#else
  #define RX_STATUS_PIN        2
#endif

#else
// ----------------------------------------------------------------------------
// RECEIVER (RX) PIN CONFIGURATION - Classic ESP32 Dev Module
// ----------------------------------------------------------------------------
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
  27, // CH3: Throttle
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

// Connection Status Indicator LED (GPIO 2 - Onboard Blue LED on ESP32 Dev Module)
#define RX_STATUS_PIN          2
#endif

// RX Telemetry uplink interval in milliseconds (100ms = 10Hz)
#define RX_TELEMETRY_INTERVAL_MS 100

// RX Failsafe timeout in milliseconds
#define RX_FAILSAFE_TIMEOUT_MS 500

