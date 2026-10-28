/**
 * ============================================================================
 * Module:   USB Communication & ICD Protocol Handler
 * Target:   Seeed Studio XIAO ESP32-C6
 * Protocol: Interface Control Document (ICD) SPEC-REV 2.4
 * ============================================================================
 */

#include "usb_comm.h"

// Maximum buffer size for incoming COBS frames
#define RX_BUF_SIZE 64

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
// Global User Input Instance & Direct Variable Aliases
// ============================================================================
UserInput usb_input = {
  .roll             = 1500,
  .pitch            = 1500,
  .yaw              = 1500,
  .throttle         = THROTTLE_MIN,
  .aux_channels     = 0,
  .is_armed         = false,
  .is_connected     = false,
  .failsafe         = true,
  .new_data         = false,
  .last_packet_time = 0,
  .pps              = 0
};

// Aliases bound to fields in usb_input
const uint16_t &usb_throttle  = usb_input.throttle;
const uint16_t &usb_roll      = usb_input.roll;
const uint16_t &usb_pitch     = usb_input.pitch;
const uint16_t &usb_yaw       = usb_input.yaw;
const uint16_t &usb_aux       = usb_input.aux_channels;
const bool     &usb_armed     = usb_input.is_armed;
const bool     &usb_connected = usb_input.is_connected;

// ============================================================================
// Internal State & Buffers
// ============================================================================
static uint8_t       g_rx_buf[RX_BUF_SIZE];
static size_t        g_rx_idx = 0;

static uint32_t      g_packet_counter = 0; // Packets received during the current second
static uint8_t       g_last_pps       = 0; // Packets-per-second computed for telemetry
static unsigned long g_last_pps_calc  = 0; // Timer tracking PPS measurement interval

static uint16_t      g_telemetry_voltage_mv = 0; // User-configured voltage override
static uint16_t      g_telemetry_current_ma = 0; // User-configured current override

// ============================================================================
// Integrity & Framing Utilities
// ============================================================================

/**
 * Calculates CRC-16/CCITT-FALSE checksum.
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
 */
static size_t cobs_decode(const uint8_t *src, size_t length, uint8_t *dst) {
  size_t read_idx = 0;
  size_t write_idx = 0;
  while (read_idx < length) {
    uint8_t code = src[read_idx++];
    if (code == 0) return 0; // 0x00 inside payload indicates corruption
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
// Internal Packet Handler
// ============================================================================
static void processValidPacket(const uint8_t *payload) {
  CommandPayload cmd;
  memcpy(&cmd, payload, sizeof(CommandPayload));

  // Update input variables
  usb_input.roll             = cmd.roll;
  usb_input.pitch            = cmd.pitch;
  usb_input.yaw              = cmd.yaw;
  usb_input.throttle         = cmd.throttle;
  usb_input.aux_channels     = cmd.aux_channels;
  usb_input.is_armed         = (cmd.aux_channels & 0x01) != 0;
  usb_input.is_connected     = true;
  usb_input.failsafe         = false;
  usb_input.new_data         = true;
  usb_input.last_packet_time = millis();

  g_packet_counter++;

  // Respond with live telemetry uplink
  usb_comm_send_telemetry();
}

// ============================================================================
// Public API Implementation
// ============================================================================

void usb_comm_init(uint32_t baud) {
  Serial.begin(baud);
  g_rx_idx = 0;
  g_packet_counter = 0;
  g_last_pps = 0;
  g_last_pps_calc = millis();
}

void usb_comm_set_telemetry(uint16_t voltage_mv, uint16_t current_ma) {
  g_telemetry_voltage_mv = voltage_mv;
  g_telemetry_current_ma = current_ma;
}

void usb_comm_send_telemetry() {
  TelemetryPayload telem;
  telem.crtp_header = 0x51; // ICD Telemetry Header

  // Use configured voltage or fallback to nominal 3.85V battery
  telem.voltage_mv = (g_telemetry_voltage_mv > 0) ? g_telemetry_voltage_mv : 3850;

  // Use configured current or calculate simulated current based on throttle
  if (g_telemetry_current_ma > 0) {
    telem.current_ma = g_telemetry_current_ma;
  } else {
    telem.current_ma = (uint16_t)map(usb_input.throttle, THROTTLE_MIN, THROTTLE_MAX, 200, 6500);
  }

  telem.pps = g_last_pps;

  // 1. Pack telemetry and append CRC-16
  uint8_t raw[sizeof(TelemetryPayload) + 2];
  memcpy(raw, &telem, sizeof(TelemetryPayload));

  uint16_t crc = crc16_ccitt((uint8_t *)&telem, sizeof(TelemetryPayload));
  raw[sizeof(TelemetryPayload)]     = (uint8_t)(crc & 0xFF);
  raw[sizeof(TelemetryPayload) + 1] = (uint8_t)((crc >> 8) & 0xFF);

  // 2. COBS encode and append 0x00 delimiter
  uint8_t cobs[32];
  size_t c_len = cobs_encode(raw, sizeof(raw), cobs);
  cobs[c_len++] = 0x00;

  // 3. Write over USB-CDC
  Serial.write(cobs, c_len);
  Serial.flush();
}

bool usb_comm_update() {
  usb_input.new_data = false;

  // 1. Ingest incoming USB-CDC serial stream
  while (Serial.available() > 0) {
    uint8_t b = Serial.read();

    // 0x00 delimiter marks the boundary of a COBS frame
    if (b == 0x00) {
      if (g_rx_idx > 0) {
        uint8_t decoded[RX_BUF_SIZE];
        size_t dec_len = cobs_decode(g_rx_buf, g_rx_idx, decoded);

        // Valid frame: strictly 11-byte payload + 2-byte CRC = 13 bytes, header 0x31
        if (dec_len == sizeof(CommandPayload) + 2 && decoded[0] == 0x31) {
          uint16_t rx_crc   = (uint16_t)decoded[11] | ((uint16_t)decoded[12] << 8);
          uint16_t calc_crc = crc16_ccitt(decoded, 11);

          if (rx_crc == calc_crc) {
            processValidPacket(decoded);
          }
        }
        g_rx_idx = 0; // Reset for next frame
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

  // 2. Failsafe Watchdog (>250ms contract)
  if (now - usb_input.last_packet_time > FAILSAFE_TIMEOUT_MS) {
    usb_input.throttle     = THROTTLE_MIN;
    usb_input.is_armed     = false;
    usb_input.is_connected = false;
    usb_input.failsafe     = true;
  }

  // 3. Compute PPS (Packets-Per-Second) every 1000 ms
  if (now - g_last_pps_calc >= 1000) {
    g_last_pps_calc = now;
    g_last_pps = (g_packet_counter > 255) ? 255 : (uint8_t)g_packet_counter;
    g_packet_counter = 0;
    usb_input.pps = g_last_pps;
  }

  return usb_input.new_data;
}
