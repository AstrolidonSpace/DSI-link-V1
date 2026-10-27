/**
 * ============================================================================
 * Project:  DSI Link Drone Receiver & Throttle LED Indicator
 * Target:   Seeed Studio XIAO ESP32-C6
 * Protocol: Interface Control Document (ICD) SPEC-REV 2.4
 * 
 * Overview:
 *   - Direct USB-CDC (ACM) communication with the mobile control application.
 *   - Downlink (0x31): 11-byte CRTP setpoint + 2-byte CRC-16 (COBS framed + 0x00).
 *   - Uplink   (0x51): 6-byte CRTP telemetry + 2-byte CRC-16 (COBS framed + 0x00).
 *   - Failsafe: Disarms / cuts throttle if no valid frame is received for >250 ms.
 *   - Actuator: Built-in user LED on GPIO 15 glows proportionally to throttle.
 * ============================================================================
 */

#include <Arduino.h>

// ============================================================================
// Hardware & Timing Configuration
// ============================================================================

// On the Seeed Studio XIAO ESP32-C6, the user LED is on GPIO 15 (Active-LOW)
#ifndef LED_BUILTIN
  #define LED_BUILTIN 15
#endif

// LED PWM Parameters (LEDC Peripheral)
#define LED_PWM_FREQ        5000  // 5 kHz PWM frequency (eliminates visible flicker)
#define LED_PWM_RES         8     // 8-bit duty resolution (0 to 255)

// Control & Failsafe Parameters
#define THROTTLE_MIN        1000  // Cutoff / Idle throttle in microseconds
#define THROTTLE_MAX        2000  // Full throttle in microseconds
#define FAILSAFE_TIMEOUT_MS 250   // Maximum allowable packet loss before failsafe kicks in

// Maximum buffer size for receiving incoming COBS frames
#define RX_BUF_SIZE         64

// ============================================================================
// ICD Data Structures (Packed, Little-Endian)
// ============================================================================
#pragma pack(push, 1)

/**
 * Downlink Command Setpoint (Port 0x03 Commander, Channel 0x01)
 * Total raw size: strictly 11 bytes.
 */
typedef struct {
  uint8_t  crtp_header;   // 0x31: Commander Port 0x03, Sub-channel 0x01
  uint16_t roll;          // 1000 - 2000 µs (1500 = Neutral Center)
  uint16_t pitch;         // 1000 - 2000 µs (1500 = Neutral Center)
  uint16_t yaw;           // 1000 - 2000 µs (1500 = Neutral Center)
  uint16_t throttle;      // 1000 - 2000 µs (1000 = Cutoff / Idle)
  uint16_t aux_channels;  // 16-bit bitfield (Bit 0 = Primary Arming toggle)
} CommandPayload;

/**
 * Uplink Telemetry Packet (Port 0x05 Logging, Channel 0x01)
 * Total raw size: strictly 6 bytes.
 */
typedef struct {
  uint8_t  crtp_header;   // 0x51: Logging Port 0x05, Sub-channel 0x01
  uint16_t voltage_mv;    // Battery voltage in millivolts (1 LSB = 1 mV, e.g. 3850 mV)
  uint16_t current_ma;    // Current draw in milliamperes (1 LSB = 1 mA)
  uint8_t  pps;           // Link Quality Index: Valid control packets / second
} TelemetryPayload;

#pragma pack(pop)

// ============================================================================
// Flight State & Metrics
// ============================================================================
static uint16_t g_throttle = THROTTLE_MIN;  // Current active throttle
static uint16_t g_roll     = 1500;          // Roll setpoint (1500 = center)
static uint16_t g_pitch    = 1500;          // Pitch setpoint (1500 = center)
static uint16_t g_yaw      = 1500;          // Yaw setpoint (1500 = center)
static uint16_t g_aux      = 0;             // Auxiliary bitfield state

static unsigned long g_last_packet_time = 0; // Timestamp of the last valid packet (ms)
static uint32_t      g_packet_counter   = 0; // Running count of valid packets in current second
static uint8_t       g_last_pps         = 0; // Computed Packets-Per-Second for telemetry
static unsigned long g_last_pps_calc    = 0; // Timer for 1-second PPS calculation

// Serial reception ring/accumulator buffer
static uint8_t g_rx_buf[RX_BUF_SIZE];
static size_t  g_rx_idx = 0;

// ============================================================================
// Integrity & Framing Utilities
// ============================================================================

/**
 * Calculates the standard CRC-16/CCITT-FALSE checksum.
 * Polynomial: 0x1021, Initial: 0xFFFF, RefIn: false, RefOut: false, XorOut: 0x0000.
 */
static uint16_t crc16_ccitt(const uint8_t *data, size_t length) {
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

/**
 * Decodes a Consistent Overhead Byte Stuffing (COBS) byte stream.
 * Restores original 0x00 bytes that were replaced during encoding.
 * 
 * @param src     COBS-encoded input bytes (without the trailing 0x00 delimiter).
 * @param length  Length of the encoded byte array.
 * @param dst     Destination buffer to write decoded raw bytes.
 * @return        Number of decoded bytes written to dst (0 if stream is invalid).
 */
static size_t cobs_decode(const uint8_t *src, size_t length, uint8_t *dst) {
  size_t read_idx = 0;
  size_t write_idx = 0;
  while (read_idx < length) {
    uint8_t code = src[read_idx++];
    if (code == 0) return 0; // 0x00 inside COBS payload indicates corruption
    for (uint8_t i = 1; i < code; i++) {
      if (read_idx >= length) return 0;
      dst[write_idx++] = src[read_idx++];
    }
    if (code < 0xFF && read_idx < length) {
      dst[write_idx++] = 0x00;
    }
  }
  return write_idx;
}

/**
 * Encodes a raw byte buffer into COBS format.
 * Guarantees the output contains zero 0x00 bytes, allowing 0x00 to act as frame delimiter.
 * 
 * @param src     Raw input bytes to encode.
 * @param length  Length of the input bytes.
 * @param dst     Destination buffer for COBS-encoded data.
 * @return        Length of the encoded byte array in dst.
 */
static size_t cobs_encode(const uint8_t *src, size_t length, uint8_t *dst) {
  size_t read_idx = 0;
  size_t write_idx = 1;
  size_t code_idx = 0;
  uint8_t code = 1;

  while (read_idx < length) {
    if (src[read_idx] == 0) {
      dst[code_idx] = code;
      code_idx = write_idx++;
      code = 1;
      read_idx++;
    } else {
      dst[write_idx++] = src[read_idx++];
      code++;
      if (code == 0xFF) {
        dst[code_idx] = code;
        code_idx = write_idx++;
        code = 1;
      }
    }
  }
  dst[code_idx] = code;
  return write_idx;
}

// ============================================================================
// Actuator / Indicator Drivers
// ============================================================================

/**
 * Modulates the built-in LED brightness according to the throttle command.
 * 
 * Circuit Note:
 *   The user LED on Seeed Studio XIAO ESP32-C6 is ACTIVE-LOW:
 *     - Output LOW  (0V)   -> Current flows, LED turns ON (100% brightness).
 *     - Output HIGH (3.3V) -> No current, LED turns OFF (0% brightness).
 * 
 * Inverted PWM Mapping:
 *   Duty Cycle = 255 - Brightness
 *   - At Cutoff (1000 µs): Brightness = 0   => Duty = 255 (LED completely OFF)
 *   - At Full   (2000 µs): Brightness = 255 => Duty = 0   (LED 100% full glow)
 *   - Applies quadratic gamma curve (norm^2) for natural human-eye perception.
 */
static void updateLedThrottle(uint16_t throttle_us) {
  uint16_t clamped = constrain(throttle_us, THROTTLE_MIN, THROTTLE_MAX);
  float norm = (float)(clamped - THROTTLE_MIN) / (float)(THROTTLE_MAX - THROTTLE_MIN);
  uint8_t brightness = (uint8_t)(norm * norm * 255.0f);
  ledcWrite(LED_BUILTIN, 255 - brightness);
}

// ============================================================================
// Uplink Telemetry Transmitter
// ============================================================================

/**
 * Constructs and transmits the 6-byte CRTP telemetry frame over USB-CDC.
 * 
 * Frame Composition:
 *   [6 bytes TelemetryPayload] + [2 bytes CRC16 Little-Endian]
 *   -> COBS-encoded and terminated with a 0x00 frame delimiter.
 */
static void sendTelemetry() {
  TelemetryPayload telem;
  telem.crtp_header = 0x51;     // ICD Telemetry Header
  telem.voltage_mv  = 3850;     // Simulated 3.85V battery voltage
  telem.current_ma  = (uint16_t)map(g_throttle, THROTTLE_MIN, THROTTLE_MAX, 200, 6500); // Current draw
  telem.pps         = g_last_pps; // Live Packets-Per-Second

  // 1. Pack telemetry and compute CRC-16 over the 6 raw bytes
  uint8_t raw[sizeof(TelemetryPayload) + 2];
  memcpy(raw, &telem, sizeof(TelemetryPayload));

  uint16_t crc = crc16_ccitt((uint8_t *)&telem, sizeof(TelemetryPayload));
  raw[sizeof(TelemetryPayload)]     = (uint8_t)(crc & 0xFF);        // CRC Little-Endian LSB
  raw[sizeof(TelemetryPayload) + 1] = (uint8_t)((crc >> 8) & 0xFF); // CRC Little-Endian MSB

  // 2. COBS encode and append 0x00 frame delimiter
  uint8_t cobs[32];
  size_t c_len = cobs_encode(raw, sizeof(raw), cobs);
  cobs[c_len++] = 0x00;

  // 3. Push immediately over USB-CDC
  Serial.write(cobs, c_len);
  Serial.flush();
}

// ============================================================================
// Downlink Command Handler
// ============================================================================

/**
 * Parses and executes a verified 11-byte control command setpoint.
 */
static void handlePacket(const uint8_t *payload) {
  CommandPayload cmd;
  memcpy(&cmd, payload, sizeof(CommandPayload));

  // Extract flight setpoints
  g_roll     = cmd.roll;
  g_pitch    = cmd.pitch;
  g_yaw      = cmd.yaw;
  g_throttle = cmd.throttle;
  g_aux      = cmd.aux_channels;

  // Update timestamps and metrics
  g_last_packet_time = millis();
  g_packet_counter++;

  // Actuate hardware LED and send return telemetry
  updateLedThrottle(g_throttle);
  sendTelemetry();
}

// ============================================================================
// System Setup
// ============================================================================
void setup() {
  // Initialize native USB-CDC interface at 115200 baud
  Serial.begin(115200);

  // Initialize LEDC PWM channel on the built-in LED pin
  ledcAttach(LED_BUILTIN, LED_PWM_FREQ, LED_PWM_RES);

  // Default state: LED is OFF (Active-LOW => Duty = 255)
  ledcWrite(LED_BUILTIN, 255);
}

// ============================================================================
// Main Execution Loop
// ============================================================================
void loop() {
  // 1. Read incoming stream from USB-CDC
  while (Serial.available() > 0) {
    uint8_t b = Serial.read();

    // 0x00 marks the end of a COBS-encoded packet
    if (b == 0x00) {
      if (g_rx_idx > 0) {
        uint8_t decoded[RX_BUF_SIZE];
        size_t dec_len = cobs_decode(g_rx_buf, g_rx_idx, decoded);

        // Valid frame: exactly 11 bytes payload + 2 bytes CRC = 13 bytes, header 0x31
        if (dec_len == sizeof(CommandPayload) + 2 && decoded[0] == 0x31) {
          uint16_t rx_crc   = (uint16_t)decoded[11] | ((uint16_t)decoded[12] << 8);
          uint16_t calc_crc = crc16_ccitt(decoded, 11);

          // Only apply control command if CRC-16 check passes
          if (rx_crc == calc_crc) {
            handlePacket(decoded);
          }
        }
        g_rx_idx = 0; // Reset buffer for next packet
      }
      continue;
    }

    // Accumulate incoming payload bytes
    if (g_rx_idx < RX_BUF_SIZE - 1) {
      g_rx_buf[g_rx_idx++] = b;
    } else {
      g_rx_idx = 0; // Discard on overflow
    }
  }

  unsigned long now = millis();

  // 2. Airborne Failsafe Watchdog (>250ms contract)
  // If control packets stop arriving, disarm and extinguish the throttle LED
  if (now - g_last_packet_time > FAILSAFE_TIMEOUT_MS) {
    g_throttle = THROTTLE_MIN;
    ledcWrite(LED_BUILTIN, 255); // LED OFF
  }

  // 3. Compute Packets-Per-Second (PPS) once every 1000 ms
  if (now - g_last_pps_calc >= 1000) {
    g_last_pps_calc = now;
    g_last_pps = (g_packet_counter > 255) ? 255 : (uint8_t)g_packet_counter;
    g_packet_counter = 0;
  }
}