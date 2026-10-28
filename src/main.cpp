/**
 * ============================================================================
 * Project:  DSI Link Drone Receiver & Throttle LED Indicator
 * Target:   Seeed Studio XIAO ESP32-C6
 * Protocol: Interface Control Document (ICD) SPEC-REV 2.4
 * 
 * Overview:
 *   - USB communication, COBS decoding, CRC checking, and telemetry are
 *     encapsulated in "usb_comm.h" / "usb_comm.cpp".
 *   - User inputs can be directly accessed via:
 *       • Struct:  `usb_input.throttle`, `usb_input.roll`, `usb_input.pitch`,
 *                  `usb_input.yaw`, `usb_input.aux_channels`, `usb_input.is_armed`
 *       • Aliases: `usb_throttle`, `usb_roll`, `usb_pitch`, `usb_yaw`,
 *                  `usb_aux`, `usb_armed`, `usb_connected`
 *   - Failsafe: automatically cuts throttle and resets arm state after >250ms.
 *   - Actuator: Built-in user LED on GPIO 15 glows proportionally to throttle.
 * ============================================================================
 */

#include <Arduino.h>
#include "usb_comm.h"

// ============================================================================
// Hardware Configuration
// ============================================================================

// On the Seeed Studio XIAO ESP32-C6, user LED is on GPIO 15 (Active-LOW)
#ifndef LED_BUILTIN
  #define LED_BUILTIN 15
#endif

// LED PWM Parameters (LEDC Peripheral)
#define LED_PWM_FREQ 5000  // 5 kHz PWM frequency (eliminates visible flicker)
#define LED_PWM_RES  8     // 8-bit duty resolution (0 to 255)

// ============================================================================
// Actuator / Indicator Drivers
// ============================================================================

/**
 * Modulates the built-in LED brightness according to the throttle command.
 * 
 * Circuit Note:
 *   Active-LOW LED on XIAO ESP32-C6:
 *     - Output LOW  (0V)   -> Current flows, LED turns ON (100% brightness).
 *     - Output HIGH (3.3V) -> No current, LED turns OFF (0% brightness).
 * 
 * Inverted PWM Mapping:
 *   Duty Cycle = 255 - Brightness
 *   - At Cutoff (1000 µs): Brightness = 0   => Duty = 255 (LED OFF)
 *   - At Full   (2000 µs): Brightness = 255 => Duty = 0   (LED 100% ON)
 */
static void updateLedThrottle(uint16_t throttle_us) {
  uint16_t clamped = constrain(throttle_us, THROTTLE_MIN, THROTTLE_MAX);
  float norm = (float)(clamped - THROTTLE_MIN) / (float)(THROTTLE_MAX - THROTTLE_MIN);
  uint8_t brightness = (uint8_t)(norm * norm * 255.0f);
  ledcWrite(LED_BUILTIN, 255 - brightness);
}

// ============================================================================
// System Setup
// ============================================================================
void setup() {
  // Initialize USB-CDC interface at 115200 baud
  usb_comm_init(115200);

  // Initialize LEDC PWM channel on the built-in LED pin
  ledcAttach(LED_BUILTIN, LED_PWM_FREQ, LED_PWM_RES);

  // Default state: LED is OFF (Active-LOW => Duty = 255)
  ledcWrite(LED_BUILTIN, 255);
}

// ============================================================================
// Main Execution Loop
// ============================================================================
void loop() {
  // 1. Process incoming USB packets and handle failsafe timeout
  usb_comm_update();

  // 2. Control Actuators using user input variables:
  //
  // Available input variables:
  //   • usb_throttle    (1000 - 2000 µs, 1000 = Cutoff)
  //   • usb_roll        (1000 - 2000 µs, 1500 = Center)
  //   • usb_pitch       (1000 - 2000 µs, 1500 = Center)
  //   • usb_yaw         (1000 - 2000 µs, 1500 = Center)
  //   • usb_aux         (16-bit aux bitfield)
  //   • usb_armed       (bool, true if arm bit active)
  //   • usb_connected   (bool, true if link is alive)
  //
  // Or via struct: `usb_input.throttle`, `usb_input.roll`, etc.

  if (usb_connected) {
    // Drive LED brightness with the throttle input variable
    updateLedThrottle(usb_throttle);
  } else {
    // Failsafe state: Link lost / timed out -> Extinguish LED
    ledcWrite(LED_BUILTIN, 255);
  }
}