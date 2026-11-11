/**
 * ============================================================================
 * Module:   8-Channel 12-Bit @ 50Hz PWM Receiver
 * Target:   ESP32 Dev Module (and compatible ESP32 boards)
 * ============================================================================
 */

#include "rx_pwm.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>

// ============================================================================
// Global State & Aliases
// ============================================================================
RxChannelState rx_state = {
  .channels           = { 1500, 1500, 1000, 1500, 1000, 1000, 1000, 1000, 1000 },
  .last_packet_time   = 0,
  .packets_received   = 0,
  .pps                = 0,
  .failsafe           = true,
  .transmitter_mac    = { 0 },
  .transmitter_paired = false
};

static uint8_t s_raw_aux5 = 0;

// Aliases bound to channel state
const uint16_t &rx_ch1_roll        = rx_state.channels[0];
const uint16_t &rx_ch2_pitch       = rx_state.channels[1];
const uint16_t &rx_ch3_throttle    = rx_state.channels[2];
const uint16_t &rx_ch4_yaw         = rx_state.channels[3];
const uint16_t &rx_ch5_aux1        = rx_state.channels[4];
const uint16_t &rx_ch6_aux2        = rx_state.channels[5];
const uint16_t &rx_ch7_aux3        = rx_state.channels[6];
const uint16_t &rx_ch8_aux4        = rx_state.channels[7];
const uint16_t &rx_ch9_aux5        = rx_state.channels[8];
const uint8_t  &rx_raw_aux5        = s_raw_aux5;
const bool     &rx_failsafe_active = rx_state.failsafe;


// Internal packet tracking for 500ms telemetry window
static uint16_t      g_packets_in_window = 0;
static unsigned long g_last_telem_send   = 0;

// ============================================================================
// PWM Hardware Control (12-bit resolution @ 50Hz)
// ============================================================================

void rx_pwm_write_channel(uint8_t ch, uint16_t pulse_us) {
  if (ch >= RX_NUM_CHANNELS) return;

  uint16_t clamped = constrain(pulse_us, 900, 2100);

  // At 50Hz (20,000 µs period) with 12-bit (0 - 4095 count):
  //   Duty = (pulse_us * 4095) / 20000
  //   1000 µs => ~205 count
  //   1500 µs => ~307 count
  //   2000 µs => ~410 count
  uint32_t duty = (uint32_t)((clamped * 4095UL + 10000UL) / 20000UL);

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(RX_PWM_PINS[ch], duty);
#else
  #if defined(CONFIG_IDF_TARGET_ESP32S3)
    if (ch < 8) {
      ledcWrite(ch, duty);
    }
  #else
    ledcWrite(ch, duty);
  #endif
#endif
}

// ============================================================================
// Battery Monitor ADC
// ============================================================================

uint16_t rx_read_battery_mv() {
#if defined(RX_BATTERY_PIN) && (RX_BATTERY_PIN < 40)
  int raw = analogRead(RX_BATTERY_PIN);
  if (raw > 50) {
    float pin_voltage = (float)raw * (3.3f / 4095.0f);
    float battery_v   = pin_voltage * RX_BATTERY_DIVIDER;
    return (uint16_t)(battery_v * 1000.0f);
  }
#endif
  return 3850; // Nominal 3.85V battery voltage fallback
}

// ============================================================================
// ESP-NOW Receive Callback
// ============================================================================

#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void onRxDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  const uint8_t *src_mac = info ? info->src_addr : nullptr;
#else
static void onRxDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
  const uint8_t *src_mac = mac_addr;
#endif

  if (len == sizeof(flight_control_packet_t)) {
    flight_control_packet_t pkt;
    memcpy(&pkt, incomingData, sizeof(flight_control_packet_t));

    // Verify CRC-16 integrity before accepting packet
    if (espnow_verify_crc16(pkt)) {
      rx_state.packets_received++;
      g_packets_in_window++;
      rx_state.last_packet_time = millis();
      rx_state.failsafe         = false;

      // Successful connection achieved: Output HIGH on GPIO 2
      digitalWrite(RX_STATUS_PIN, HIGH);


      // Auto-pair with transmitter MAC address for return telemetry
      if (src_mac != nullptr && !rx_state.transmitter_paired) {
        memcpy(rx_state.transmitter_mac, src_mac, 6);
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, src_mac, 6);
        peerInfo.channel = 0;
        peerInfo.encrypt = false;
        if (!esp_now_is_peer_exist(src_mac)) {
          esp_now_add_peer(&peerInfo);
        }
        rx_state.transmitter_paired = true;
      }

      // 1. Map 4 primary flight channels (1000 - 2000 µs)
      rx_state.channels[0] = constrain(pkt.roll, 1000, 2000);     // CH1: Roll
      rx_state.channels[1] = constrain(pkt.pitch, 1000, 2000);    // CH2: Pitch
      rx_state.channels[2] = constrain(pkt.throttle, 1000, 2000); // CH3: Throttle
      rx_state.channels[3] = constrain(pkt.yaw, 1000, 2000);      // CH4: Yaw

      // 2. Decode 5 AUX channels from 16-bit bitfield (per ICD SPEC-REV 2.4):
      // CH5 (AUX 1 - Arm): Bit 0 (1-bit: 0 or 1)
      rx_state.channels[4] = (pkt.aux_channels & 0x0001) ? 2000 : 1000;

      // CH6 (AUX 2): Bits 1-3 (3-bit: 0 to 7) -> 2000 µs only at value 7
      uint8_t aux2 = (pkt.aux_channels >> 1) & 0x07;
      rx_state.channels[5] = map(aux2, 0, 7, 1000, 2000);

      // CH7 (AUX 3): Bits 4-6 (3-bit: 0 to 7) -> 2000 µs only at value 7
      uint8_t aux3 = (pkt.aux_channels >> 4) & 0x07;
      rx_state.channels[6] = map(aux3, 0, 7, 1000, 2000);

      // CH8 (AUX 4): Bits 7-9 (3-bit: 0 to 7) -> 2000 µs only at value 7
      uint8_t aux4 = (pkt.aux_channels >> 7) & 0x07;
      rx_state.channels[7] = map(aux4, 0, 7, 1000, 2000);

      // CH9 (AUX 5): Bits 10-15 (6-bit: 0 to 63)
      s_raw_aux5 = (pkt.aux_channels >> 10) & 0x3F;
      rx_state.channels[8] = map(s_raw_aux5, 0, 63, 1000, 2000);

      // 3. Immediately update hardware PWM outputs with 12-bit precision
      for (uint8_t i = 0; i < RX_NUM_CHANNELS; i++) {
        rx_pwm_write_channel(i, rx_state.channels[i]);
      }
      Serial.printf("CH1: %d, CH2: %d, CH3: %d, CH4: %d, CH5: %d, CH6: %d, CH7: %d, CH8: %d, CH9: %d\n", rx_state.channels[0], rx_state.channels[1], rx_state.channels[2], rx_state.channels[3], rx_state.channels[4], rx_state.channels[5], rx_state.channels[6], rx_state.channels[7], rx_state.channels[8]);
    }
  }
}


// ============================================================================
// Public API Implementation
// ============================================================================

bool rx_pwm_init() {
  // 1. Initialize LEDC hardware for all 9 PWM output channels
  for (uint8_t i = 0; i < RX_NUM_CHANNELS; i++) {
    uint8_t pin = RX_PWM_PINS[i];
    pinMode(pin, OUTPUT);

#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(pin, RX_PWM_FREQ, RX_PWM_RES);
#else
  #if defined(CONFIG_IDF_TARGET_ESP32S3)
    if (i < 8) {
      ledcSetup(i, RX_PWM_FREQ, RX_PWM_RES);
      ledcAttachPin(pin, i);
    }
  #else
    ledcSetup(i, RX_PWM_FREQ, RX_PWM_RES);
    ledcAttachPin(pin, i);
  #endif
#endif

    // Initial safe outputs (Neutral center on flight channels, Cutoff on throttle)
    rx_pwm_write_channel(i, rx_state.channels[i]);
  }

  // Initialize Connection Status Indicator (GPIO 2 - Onboard LED)
  pinMode(RX_STATUS_PIN, OUTPUT);
  digitalWrite(RX_STATUS_PIN, LOW); // Initially LOW until connection is established

  // 2. Initialize Wi-Fi in Station mode
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Print RX MAC address immediately after bootup
  uint8_t mac[6];
  WiFi.macAddress(mac);
  Serial.println();
  Serial.println("==================================================");
  Serial.printf(" [RX] MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.printf(" [RX] C-Array: {0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X}\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.println("==================================================");
  Serial.println();

  // 3. Enable Wi-Fi Long Range (LR) protocol
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);

  // 4. Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("[RX] ESP-NOW init failed!");
    return false;
  }

  // 5. Register receive callback
  esp_now_register_recv_cb(onRxDataRecv);

  g_last_telem_send = millis();
  Serial.println("[RX] 9-Channel 12-bit @ 50Hz PWM Receiver initialized.");
  return true;
}

void rx_pwm_update() {
  unsigned long now = millis();

  // 1. Failsafe Watchdog (>500ms without packet)
  if (!rx_state.failsafe && (now - rx_state.last_packet_time > RX_FAILSAFE_TIMEOUT_MS)) {
    rx_state.failsafe = true;

    // Disconnected / Failsafe: Output LOW on GPIO 2
    digitalWrite(RX_STATUS_PIN, LOW);

    // Apply safe failsafe outputs
    rx_state.channels[0] = 1500; // Roll Center
    rx_state.channels[1] = 1500; // Pitch Center
    rx_state.channels[2] = 1000; // Throttle Cutoff
    rx_state.channels[3] = 1500; // Yaw Center
    rx_state.channels[4] = 1000; // Disarm (AUX 1)
    rx_state.channels[5] = 1000; // AUX 2
    rx_state.channels[6] = 1000; // AUX 3
    rx_state.channels[7] = 1000; // AUX 4
    rx_state.channels[8] = 1000; // AUX 5
    s_raw_aux5           = 0;
    rx_state.pps         = 0;

    for (uint8_t i = 0; i < RX_NUM_CHANNELS; i++) {
      rx_pwm_write_channel(i, rx_state.channels[i]);
    }
  }

  // 2. Transmit Telemetry Uplink back to Transmitter at 10 Hz (every 100ms)
  if (now - g_last_telem_send >= RX_TELEMETRY_INTERVAL_MS) {
    g_last_telem_send = now;

    // Rolling 1-second window (10 slots of 100ms) for accurate, jitter-free PPS
    static uint8_t s_pps_history[10] = {0};
    static uint8_t s_pps_slot = 0;

    s_pps_history[s_pps_slot] = g_packets_in_window;
    s_pps_slot = (s_pps_slot + 1) % 10;

    uint16_t total_1sec_packets = 0;
    for (uint8_t i = 0; i < 10; i++) {
      total_1sec_packets += s_pps_history[i];
    }
    rx_state.pps = total_1sec_packets;

    if (rx_state.transmitter_paired) {
      telemetry_packet_t telem;
      telem.packet_id          = 0;
      telem.packet_count_500ms = (rx_state.pps / 2); // Converts so TX (packet_count * 2) matches 1-sec PPS
      telem.battery_mV         = rx_read_battery_mv();

      esp_now_send(rx_state.transmitter_mac, (const uint8_t *)&telem, sizeof(telem));
    }

    g_packets_in_window = 0;
  }
}

