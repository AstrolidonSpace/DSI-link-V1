/**
 * ============================================================================
 * Project:  DSI Link Drone System (Dual-Role TX / RX Firmware)
 * Target:   
 *   - Transmitter (TX): Seeed Studio XIAO ESP32-C6 / ESP32-C3
 *   - Receiver    (RX): ESP32 Dev Module (8-channel 12-bit PWM @ 50Hz)
 * ============================================================================
 */

#include <Arduino.h>
#include "config.h"

#if defined(ROLE_RECEIVER)
// ============================================================================
// RECEIVER FIRMWARE (ESP32 Dev Module)
// ============================================================================
#include "rx_pwm.h"

void setup() {
  Serial.begin(115200);
  delay(300); // Allow serial monitor time to establish connection

  // Initialize 9-channel 12-bit @ 50Hz PWM and ESP-NOW Long Range receiver
  rx_pwm_init();
  Serial.println("[SYSTEM] Running as RECEIVER: 9-Channel 12-bit @ 50Hz PWM Active");
}


void loop() {
  // 1. Manages failsafe timeout and 500ms telemetry uplink back to transmitter
  rx_pwm_update();

  // Note:
  //   • Hardware PWM outputs on CH1-CH9 update automatically with every verified packet.
  //   • Live channel states available anytime:
  //       - rx_ch1_roll        (1000 - 2000 µs)
  //       - rx_ch2_pitch       (1000 - 2000 µs)
  //       - rx_ch3_throttle    (1000 - 2000 µs)
  //       - rx_ch4_yaw         (1000 - 2000 µs)
  //       - rx_ch5_aux1        (1000 / 2000 µs Arm switch)
  //       - rx_ch6_aux2        (1000 - 2000 µs Mode switch)
  //       - rx_ch7_aux3        (1000 - 2000 µs)
  //       - rx_ch8_aux4        (1000 - 2000 µs)
  //       - rx_ch9_aux5        (1000 - 2000 µs on GPIO 18)
  //       - rx_raw_aux5        (raw 6-bit value: 0 - 63)
  //       - rx_failsafe_active (bool, true if connection lost)

}

#else
// ============================================================================
// TRANSMITTER FIRMWARE (Seeed Studio XIAO ESP32-C6 / ESP32-C3)
// ============================================================================
#include "usb_comm.h"
#include "espnow_link.h"
#include <esp_arduino_version.h>

#ifndef LED_BUILTIN
  #define LED_BUILTIN 15
#endif

#define LED_PWM_FREQ    5000  // 5 kHz PWM frequency
#define LED_PWM_RES     8     // 8-bit duty resolution (0 to 255)
#define LED_PWM_CHANNEL 0     // Channel used for Arduino Core 2.x

static void updateLedThrottle(uint16_t throttle_us) {
  uint16_t clamped = constrain(throttle_us, THROTTLE_MIN, THROTTLE_MAX);
  float norm = (float)(clamped - THROTTLE_MIN) / (float)(THROTTLE_MAX - THROTTLE_MIN);
  uint8_t brightness = (uint8_t)(norm * norm * 255.0f);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(LED_BUILTIN, 255 - brightness);
#else
  ledcWrite(LED_PWM_CHANNEL, 255 - brightness);
#endif
}

void setup() {
  // 1. Initialize native USB-CDC interface at 115200 baud
  usb_comm_init(115200);

  // 2. Initialize ESP-NOW Long Range (LR) wireless link
  //    Launches the strict 50Hz FreeRTOS task and registers telemetry callbacks
  espnow_link_init();

  // 3. Initialize LEDC PWM channel on the built-in LED pin
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(LED_BUILTIN, LED_PWM_FREQ, LED_PWM_RES);
  ledcWrite(LED_BUILTIN, 255); // LED OFF initially
#else
  ledcSetup(LED_PWM_CHANNEL, LED_PWM_FREQ, LED_PWM_RES);
  ledcAttachPin(LED_BUILTIN, LED_PWM_CHANNEL);
  ledcWrite(LED_PWM_CHANNEL, 255); // LED OFF initially
#endif

  Serial.println("[SYSTEM] Running as TRANSMITTER: USB Phone App <-> ESP-NOW 50Hz");
}

void loop() {
  // 1. Process incoming USB packets and handle phone failsafe watchdog
  usb_comm_update();

  // 2. Local Actuator: Drive built-in LED according to user throttle
  if (usb_connected) {
    updateLedThrottle(usb_throttle);
  } else {
    // Phone disconnected / timed out (>250ms) -> LED OFF
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(LED_BUILTIN, 255);
#else
    ledcWrite(LED_PWM_CHANNEL, 255);
#endif
  }

  // Note:
  //   • Strict 50Hz ESP-NOW transmissions are executed automatically in the background
  //     by the FreeRTOS task (espnow_50hz) in "espnow_link.cpp".
  //   • Real-time drone telemetry (battery voltage, PPS) is automatically forwarded
  //     to the Android phone app without colliding with incoming control packets.
}

#endif