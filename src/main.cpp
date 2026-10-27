#include <Arduino.h>

#ifndef LED_BUILTIN
  #define LED_BUILTIN 15
#endif

#define LED_PWM_FREQ        5000
#define LED_PWM_RES         8
#define THROTTLE_MIN        1000
#define THROTTLE_MAX        2000
#define FAILSAFE_TIMEOUT_MS 250

#define RX_BUF_SIZE         64

#pragma pack(push, 1)
typedef struct {
  uint8_t  crtp_header;
  uint16_t roll;
  uint16_t pitch;
  uint16_t yaw;
  uint16_t throttle;
  uint16_t aux_channels;
} CommandPayload;

typedef struct {
  uint8_t  crtp_header;
  uint16_t voltage_mv;
  uint16_t current_ma;
  uint8_t  pps;
} TelemetryPayload;
#pragma pack(pop)

static uint16_t g_throttle = THROTTLE_MIN;
static uint16_t g_roll     = 1500;
static uint16_t g_pitch    = 1500;
static uint16_t g_yaw      = 1500;
static uint16_t g_aux      = 0;

static unsigned long g_last_packet_time = 0;
static uint32_t      g_packet_counter   = 0;
static uint8_t       g_last_pps         = 0;
static unsigned long g_last_pps_calc    = 0;

static uint8_t g_rx_buf[RX_BUF_SIZE];
static size_t  g_rx_idx = 0;

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

static size_t cobs_decode(const uint8_t *src, size_t length, uint8_t *dst) {
  size_t read_idx = 0;
  size_t write_idx = 0;
  while (read_idx < length) {
    uint8_t code = src[read_idx++];
    if (code == 0) return 0;
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

static void updateLedThrottle(uint16_t throttle_us) {
  uint16_t clamped = constrain(throttle_us, THROTTLE_MIN, THROTTLE_MAX);
  float norm = (float)(clamped - THROTTLE_MIN) / (float)(THROTTLE_MAX - THROTTLE_MIN);
  uint8_t brightness = (uint8_t)(norm * norm * 255.0f);
  ledcWrite(LED_BUILTIN, 255 - brightness);
}

static void sendTelemetry() {
  TelemetryPayload telem;
  telem.crtp_header = 0x51;
  telem.voltage_mv  = 3850;
  telem.current_ma  = (uint16_t)map(g_throttle, THROTTLE_MIN, THROTTLE_MAX, 200, 6500);
  telem.pps         = g_last_pps;

  uint8_t raw[sizeof(TelemetryPayload) + 2];
  memcpy(raw, &telem, sizeof(TelemetryPayload));

  uint16_t crc = crc16_ccitt((uint8_t *)&telem, sizeof(TelemetryPayload));
  raw[sizeof(TelemetryPayload)]     = (uint8_t)(crc & 0xFF);
  raw[sizeof(TelemetryPayload) + 1] = (uint8_t)((crc >> 8) & 0xFF);

  uint8_t cobs[32];
  size_t c_len = cobs_encode(raw, sizeof(raw), cobs);
  cobs[c_len++] = 0x00;

  Serial.write(cobs, c_len);
  Serial.flush();
}

static void handlePacket(const uint8_t *payload) {
  CommandPayload cmd;
  memcpy(&cmd, payload, sizeof(CommandPayload));

  g_roll     = cmd.roll;
  g_pitch    = cmd.pitch;
  g_yaw      = cmd.yaw;
  g_throttle = cmd.throttle;
  g_aux      = cmd.aux_channels;

  g_last_packet_time = millis();
  g_packet_counter++;

  updateLedThrottle(g_throttle);
  sendTelemetry();
}

void setup() {
  Serial.begin(115200);

  ledcAttach(LED_BUILTIN, LED_PWM_FREQ, LED_PWM_RES);
  ledcWrite(LED_BUILTIN, 255);
}

void loop() {
  while (Serial.available() > 0) {
    uint8_t b = Serial.read();

    if (b == 0x00) {
      if (g_rx_idx > 0) {
        uint8_t decoded[RX_BUF_SIZE];
        size_t dec_len = cobs_decode(g_rx_buf, g_rx_idx, decoded);

        if (dec_len == sizeof(CommandPayload) + 2 && decoded[0] == 0x31) {
          uint16_t rx_crc = (uint16_t)decoded[11] | ((uint16_t)decoded[12] << 8);
          uint16_t calc_crc = crc16_ccitt(decoded, 11);

          if (rx_crc == calc_crc) {
            handlePacket(decoded);
          }
        }
        g_rx_idx = 0;
      }
      continue;
    }

    if (g_rx_idx < RX_BUF_SIZE - 1) {
      g_rx_buf[g_rx_idx++] = b;
    } else {
      g_rx_idx = 0;
    }
  }

  unsigned long now = millis();

  if (now - g_last_packet_time > FAILSAFE_TIMEOUT_MS) {
    g_throttle = THROTTLE_MIN;
    ledcWrite(LED_BUILTIN, 255);
  }

  if (now - g_last_pps_calc >= 1000) {
    g_last_pps_calc = now;
    g_last_pps = (g_packet_counter > 255) ? 255 : (uint8_t)g_packet_counter;
    g_packet_counter = 0;
  }
}