/**
 * ============================================================================
 * Module:   ESP-NOW Long Range (LR) 50Hz Wireless Link
 * Target:   Seeed Studio XIAO ESP32-C6 / ESP32-C3
 * Protocol: 16-byte Packet (Sequence ID + 4 Channels x 2B + 16-bit Aux + CRC-16)
 * Rate:     Strict 50Hz (20ms) driven by FreeRTOS vTaskDelayUntil
 * ============================================================================
 */

#include "espnow_link.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ============================================================================
// Default Constants & MAC Addresses
// ============================================================================
// Default drone peer MAC: {0x80, 0xB5, 0x4E, 0xC1, 0x10, 0xEC}
const uint8_t DEFAULT_DRONE_MAC[6] = {0xCC, 0xDB, 0xA7, 0x13, 0x55, 0xA8};

// ============================================================================
// Global State & Aliases
// ============================================================================
DroneTelemetry drone_telemetry = {
  .battery_mV        = 0,
  .battery_v         = 0.0f,
  .pps               = 0,
  .last_packet_time  = 0,
  .is_connected      = false
};

// Direct aliases for telemetry
const uint16_t &drone_battery_mv = drone_telemetry.battery_mV;
const float    &drone_battery_v  = drone_telemetry.battery_v;
const uint16_t &drone_pps        = drone_telemetry.pps;
const bool     &drone_connected  = drone_telemetry.is_connected;

// ============================================================================
// Internal State
// ============================================================================
static flight_control_packet_t s_control_data = {
  .packet_id    = 0,
  .roll         = 1500,
  .pitch        = 1500,
  .yaw          = 1500,
  .throttle     = THROTTLE_MIN,
  .aux_channels = 0,
  .crc          = 0
};

// Direct aliases for outgoing channels
const uint16_t &espnow_tx_roll     = s_control_data.roll;
const uint16_t &espnow_tx_pitch    = s_control_data.pitch;
const uint16_t &espnow_tx_yaw      = s_control_data.yaw;
const uint16_t &espnow_tx_throttle = s_control_data.throttle;
const uint16_t &espnow_tx_aux      = s_control_data.aux_channels;

static uint8_t           s_peer_mac[6];
static SemaphoreHandle_t s_control_mutex  = NULL;
static TaskHandle_t      s_tx_task_handle = NULL;
static bool              s_auto_feed      = true;
static bool              s_usb_forward    = true;

// ============================================================================
// CRC-16 Calculation & Verification
// ============================================================================

uint16_t espnow_calculate_crc16(const flight_control_packet_t &packet) {
  const uint8_t *data = (const uint8_t *)&packet;
  // CRC-16 is computed over all bytes up to the crc field (first 14 bytes)
  size_t length = sizeof(flight_control_packet_t) - sizeof(uint16_t);
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= ((uint16_t)data[i] << 8);
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x1021;
      } else {
        crc = crc << 1;
      }
    }
  }
  return crc;
}

bool espnow_verify_crc16(const flight_control_packet_t &packet) {
  return packet.crc == espnow_calculate_crc16(packet);
}

// ============================================================================
// ESP-NOW Receive Callback (Supports both Arduino Core 2.x and 3.x)
// ============================================================================
#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
#else
static void onDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
#endif
  // Check if incoming payload matches the 5-byte telemetry structure
  if (len == sizeof(telemetry_packet_t)) {
    telemetry_packet_t telem;
    memcpy(&telem, incomingData, sizeof(telemetry_packet_t));

    drone_telemetry.battery_mV       = telem.battery_mV;
    drone_telemetry.battery_v        = (float)telem.battery_mV / 1000.0f;
    drone_telemetry.pps              = telem.packet_count_500ms * 2; // Converts 500ms window into PPS
    drone_telemetry.last_packet_time = millis();
    drone_telemetry.is_connected     = true;

    // Immediately forward telemetry to the mobile app over USB-CDC without collision
    if (s_usb_forward) {
      usb_comm_send_telemetry(drone_telemetry.battery_mV, 0, drone_telemetry.pps);
    }
  }
}

// ============================================================================
// Strict 50Hz FreeRTOS Transmission Task
// ============================================================================
static void espnow_tx_task(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(ESPNOW_SEND_INTERVAL_MS); // Exactly 20ms = 50Hz

  for (;;) {
    // 1. If auto-feed is active, pull latest inputs from usb_input
    if (s_auto_feed) {
      espnow_link_feed_usb_input(usb_input);
    }

    // 2. Fetch thread-safe copy of flight control packet and compute CRC-16
    flight_control_packet_t packet_to_send;
    if (s_control_mutex != NULL && xSemaphoreTake(s_control_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      s_control_data.packet_id++;
      s_control_data.crc = espnow_calculate_crc16(s_control_data);
      packet_to_send = s_control_data;
      xSemaphoreGive(s_control_mutex);
    } else {
      s_control_data.packet_id++;
      s_control_data.crc = espnow_calculate_crc16(s_control_data);
      packet_to_send = s_control_data;
    }

    // 3. Transmit packet over ESP-NOW Long Range
    esp_now_send(s_peer_mac, (const uint8_t *)&packet_to_send, sizeof(packet_to_send));

    // 4. Check telemetry timeout (1200ms contract from ESPNOW_TX_telemetry)
    if (drone_telemetry.is_connected && (millis() - drone_telemetry.last_packet_time > ESPNOW_DISCONNECT_TIMEOUT_MS)) {
      drone_telemetry.is_connected = false;
      drone_telemetry.battery_mV   = 0;
      drone_telemetry.battery_v    = 0.0f;
      drone_telemetry.pps          = 0;
    }

    // vTaskDelayUntil guarantees zero cumulative timing drift for strict 50Hz
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }

  vTaskDelete(NULL);
}

// ============================================================================
// Public API Implementation
// ============================================================================

bool espnow_link_init(const uint8_t *peer_mac) {
  if (peer_mac != nullptr) {
    memcpy(s_peer_mac, peer_mac, 6);
  } else {
    memcpy(s_peer_mac, DEFAULT_DRONE_MAC, 6);
  }

  if (s_control_mutex == NULL) {
    s_control_mutex = xSemaphoreCreateMutex();
  }

  // 1. Configure Wi-Fi in Station Mode
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // 2. Enable ESP-NOW Long Range (LR) protocol
  esp_err_t err_proto = esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);
  if (err_proto != ESP_OK) {
    Serial.printf("[ESPNOW] Failed to set Long Range mode: %d\n", err_proto);
  }

  // 3. Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESPNOW] Initialization failed!");
    return false;
  }

  // 4. Register receive callback for drone telemetry
  esp_now_register_recv_cb(onDataRecv);

  // 5. Register Peer Info
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, s_peer_mac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (!esp_now_is_peer_exist(s_peer_mac)) {
    esp_now_add_peer(&peerInfo);
  }

  // 6. Launch strict 50Hz FreeRTOS task with high priority (priority 4)
  if (s_tx_task_handle == NULL) {
    BaseType_t res = xTaskCreate(
      espnow_tx_task,
      "espnow_50hz",
      4096,
      NULL,
      4,
      &s_tx_task_handle
    );
    if (res != pdPASS) {
      Serial.println("[ESPNOW] Failed to create 50Hz FreeRTOS task!");
      return false;
    }
  }

  Serial.println("[ESPNOW] Long Range 50Hz link initialized successfully.");
  return true;
}

void espnow_link_set_channels(uint16_t roll, uint16_t pitch, uint16_t yaw, uint16_t throttle, uint16_t aux_channels) {
  if (s_control_mutex != NULL && xSemaphoreTake(s_control_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    s_control_data.roll         = roll;
    s_control_data.pitch        = pitch;
    s_control_data.yaw          = yaw;
    s_control_data.throttle     = throttle;
    s_control_data.aux_channels = aux_channels;
    xSemaphoreGive(s_control_mutex);
  } else {
    s_control_data.roll         = roll;
    s_control_data.pitch        = pitch;
    s_control_data.yaw          = yaw;
    s_control_data.throttle     = throttle;
    s_control_data.aux_channels = aux_channels;
  }
}

void espnow_link_feed_usb_input(const UserInput &input) {
  uint16_t r   = 1500;
  uint16_t p   = 1500;
  uint16_t y   = 1500;
  uint16_t t   = THROTTLE_MIN;
  uint16_t aux = 0;

  // Forward raw 2-byte channels when USB link is alive and drone is armed
  if (input.is_connected && input.is_armed && !input.failsafe) {
    r   = input.roll;
    p   = input.pitch;
    y   = input.yaw;
    t   = input.throttle;
    aux = input.aux_channels;
  }

  espnow_link_set_channels(r, p, y, t, aux);
}

void espnow_link_enable_auto_feed(bool enable) {
  s_auto_feed = enable;
}

void espnow_link_enable_usb_forward(bool enable) {
  s_usb_forward = enable;
}

bool espnow_link_set_peer(const uint8_t *peer_mac) {
  if (peer_mac == nullptr) return false;
  memcpy(s_peer_mac, peer_mac, 6);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, s_peer_mac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_is_peer_exist(s_peer_mac)) {
    esp_now_del_peer(s_peer_mac);
  }
  return (esp_now_add_peer(&peerInfo) == ESP_OK);
}
