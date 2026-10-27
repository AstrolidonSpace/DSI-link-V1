/**
 * ============================================================================
 * Project: XIAO ESP32-C6 USB Diagnostic & Throttle LED Bridge
 * Target:  Seeed Studio XIAO ESP32-C6
 * 
 * Features:
 * 1. Full ICD SPEC-REV 2.4 USB-CDC Decoder (COBS + CRC-16/CCITT-FALSE)
 * 2. Visual LED Diagnostic Modes (Identify App vs ESP32 bug in 2 seconds)
 * 3. Built-in Wi-Fi Web Dashboard (AP: "XIAO-C6-DEBUG", IP: 192.168.4.1)
 *    -> Lets you see live USB byte counts, hex dumps, and CRC results on your phone!
 * 4. Hardware UART Mirror on GPIO 16 (TX) at 115200 baud
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// ==========================================
// Hardware Configuration
// ==========================================
#ifndef LED_BUILTIN
  #define LED_BUILTIN 15  // Active-LOW User LED on Seeed Studio XIAO ESP32-C6
#endif

#define LED_PWM_FREQ       5000  // 5 kHz PWM
#define LED_PWM_RES        8     // 8-bit resolution (0 - 255)

// Throttle Limits according to ICD (in microseconds)
#define THROTTLE_MIN       1000  // Cutoff / Idle
#define THROTTLE_MAX       2000  // Full Throttle
#define FAILSAFE_TIMEOUT_MS 250  // Failsafe timeout contract (>250ms)

// ==========================================
// ICD Data Structures (Packed, Little-Endian)
// ==========================================
#pragma pack(push, 1)

typedef struct {
  uint8_t  crtp_header;   // 0x31: Port 0x03, Channel 0x01
  uint16_t roll;          // 1000 - 2000 µs
  uint16_t pitch;         // 1000 - 2000 µs
  uint16_t yaw;           // 1000 - 2000 µs
  uint16_t throttle;      // 1000 - 2000 µs
  uint16_t aux_channels;  // 16-bit bitfield (aux1: Bit 0 = Arming)
} CommandPayload;

typedef struct {
  uint8_t  crtp_header;   // 0x51: Port 0x05, Channel 0x01
  uint16_t voltage_mv;    // Battery Voltage (1 mV LSB)
  uint16_t current_ma;    // Current Draw (1 mA LSB)
  uint8_t  pps;           // Packets per second
} TelemetryPayload;

#pragma pack(pop)

// ==========================================
// Diagnostics & Metrics Tracking
// ==========================================
volatile uint32_t g_stat_raw_bytes     = 0;
volatile uint32_t g_stat_delimiters    = 0;
volatile uint32_t g_stat_valid_packets = 0;
volatile uint32_t g_stat_cobs_errors   = 0;
volatile uint32_t g_stat_crc_errors    = 0;
volatile uint32_t g_stat_len_errors    = 0;
volatile uint32_t g_stat_hdr_errors    = 0;

volatile uint16_t g_throttle = THROTTLE_MIN;
volatile uint16_t g_roll     = 1500;
volatile uint16_t g_pitch    = 1500;
volatile uint16_t g_yaw      = 1500;
volatile uint16_t g_aux      = 0;

volatile unsigned long g_last_packet_time = 0;
volatile unsigned long g_last_rx_byte_time = 0;
volatile uint32_t      g_packet_counter   = 0;
uint8_t                g_last_pps         = 0;
bool                   g_failsafe_active  = true;
bool                   g_app_connected    = false;

char g_last_hex_dump[64] = "None";
char g_last_error_msg[64] = "Waiting for data...";
uint16_t g_last_rx_crc = 0;
uint16_t g_last_calc_crc = 0;

// Web Server for Wi-Fi Dashboard
WebServer g_server(80);

// Hardware UART mirror on GPIO 16 (TX)
HardwareSerial SerialDebug(0);

// USB RX Buffer
#define RX_BUF_SIZE 128
uint8_t g_rx_buf[RX_BUF_SIZE];
size_t  g_rx_idx = 0;

// ==========================================
// CRC-16 / CCITT-FALSE Implementation
// ==========================================
uint16_t crc16_ccitt(const uint8_t *data, size_t length) {
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

// ==========================================
// COBS Encode / Decode
// ==========================================
size_t cobs_decode(const uint8_t *src, size_t length, uint8_t *dst) {
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

size_t cobs_encode(const uint8_t *src, size_t length, uint8_t *dst) {
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

// ==========================================
// Uplink Telemetry over USB CDC
// ==========================================
void sendUsbTelemetry() {
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

// ==========================================
// Process Setpoint
// ==========================================
void processCommandSetpoint(const uint8_t *payload, size_t len) {
  CommandPayload cmd;
  memcpy(&cmd, payload, sizeof(CommandPayload));

  g_roll     = cmd.roll;
  g_pitch    = cmd.pitch;
  g_yaw      = cmd.yaw;
  g_throttle = cmd.throttle;
  g_aux      = cmd.aux_channels;

  g_last_packet_time = millis();
  g_packet_counter = g_packet_counter + 1;
  g_stat_valid_packets = g_stat_valid_packets + 1;
  g_failsafe_active = false;
  g_app_connected   = true;
  snprintf(g_last_error_msg, sizeof(g_last_error_msg), "OK (Valid 0x31 setpoint)");

  sendUsbTelemetry();
}

// ==========================================
// USB Receive & Diagnostics Engine
// ==========================================
void handleUsbInput() {
  while (Serial.available() > 0) {
    uint8_t b = Serial.read();
    g_stat_raw_bytes = g_stat_raw_bytes + 1;
    g_last_rx_byte_time = millis();

    // Check for frame delimiter 0x00
    if (b == 0x00) {
      g_stat_delimiters = g_stat_delimiters + 1;

      if (g_rx_idx > 0) {
        // Save Hex Dump of last received packet
        size_t dump_len = min((size_t)16, g_rx_idx);
        char *ptr = g_last_hex_dump;
        for (size_t i = 0; i < dump_len; i++) {
          ptr += sprintf(ptr, "%02X ", g_rx_buf[i]);
        }
        if (g_rx_idx > dump_len) sprintf(ptr, "...");

        // Decode COBS
        uint8_t decoded[RX_BUF_SIZE];
        size_t dec_len = cobs_decode(g_rx_buf, g_rx_idx, decoded);

        if (dec_len == 0) {
          g_stat_cobs_errors = g_stat_cobs_errors + 1;
          snprintf(g_last_error_msg, sizeof(g_last_error_msg), "COBS Decode Failed");
          SerialDebug.println("[ERR] COBS decode failed");
        } else if (dec_len != sizeof(CommandPayload) + 2) {
          g_stat_len_errors = g_stat_len_errors + 1;
          snprintf(g_last_error_msg, sizeof(g_last_error_msg), "Wrong Len: %u (expected 13)", dec_len);
          SerialDebug.printf("[ERR] Decoded length %u != 13\n", dec_len);
        } else if (decoded[0] != 0x31) {
          g_stat_hdr_errors = g_stat_hdr_errors + 1;
          snprintf(g_last_error_msg, sizeof(g_last_error_msg), "Wrong Header: 0x%02X (expected 0x31)", decoded[0]);
          SerialDebug.printf("[ERR] Header 0x%02X != 0x31\n", decoded[0]);
        } else {
          // Verify CRC16-CCITT
          uint16_t rx_crc = (uint16_t)decoded[11] | ((uint16_t)decoded[12] << 8);
          uint16_t calc_crc = crc16_ccitt(decoded, 11);
          g_last_rx_crc = rx_crc;
          g_last_calc_crc = calc_crc;

          if (rx_crc != calc_crc) {
            g_stat_crc_errors = g_stat_crc_errors + 1;
            snprintf(g_last_error_msg, sizeof(g_last_error_msg), "CRC Mismatch: Got 0x%04X, Calc 0x%04X", rx_crc, calc_crc);
            SerialDebug.printf("[ERR] CRC mismatch: Got 0x%04X, Calc 0x%04X\n", rx_crc, calc_crc);
          } else {
            processCommandSetpoint(decoded, 11);
          }
        }
        g_rx_idx = 0;
      }
      continue;
    }

    // Accumulate bytes
    if (g_rx_idx < RX_BUF_SIZE - 1) {
      g_rx_buf[g_rx_idx++] = b;
    } else {
      g_rx_idx = 0; // Overflow reset
      g_stat_len_errors = g_stat_len_errors + 1;
    }
  }
}

// ==========================================
// Visual Diagnostic LED Controller
// ==========================================
/**
 * Drives the built-in LED (GPIO 15, Active-LOW) with diagnostic patterns:
 * 
 * 1. Slow Breathe / Pulse (1s ON, 1s OFF):
 *    -> ZERO bytes received over USB. The Android App is NOT sending data!
 * 
 * 2. Rapid Strobe (5 fast flashes):
 *    -> Bytes ARE arriving, but COBS or CRC-16 check failed!
 * 
 * 3. Short Double Blink:
 *    -> Valid link was established, but timed out (>250ms).
 * 
 * 4. Solid / Smooth Glow:
 *    -> ALL SYSTEMS OK! Brightness corresponds directly to throttle level.
 */
void updateDiagnosticLed() {
  unsigned long now = millis();

  // CASE 1: Normal Operation (Valid setpoints arriving within 250ms)
  if (!g_failsafe_active && g_app_connected) {
    uint16_t clamped = constrain(g_throttle, THROTTLE_MIN, THROTTLE_MAX);
    float norm = (float)(clamped - THROTTLE_MIN) / (float)(THROTTLE_MAX - THROTTLE_MIN);
    uint8_t brightness = (uint8_t)(norm * norm * 255.0f);
    // Active-LOW: 255 - brightness
    ledcWrite(LED_BUILTIN, 255 - brightness);
    return;
  }

  // CASE 2: No USB bytes ever received (App not sending / no USB permission)
  if (g_stat_raw_bytes == 0) {
    // Slow steady pulse: 1000ms OFF, 1000ms ON
    bool on = (now % 2000) < 1000;
    ledcWrite(LED_BUILTIN, on ? 0 : 255);
    return;
  }

  // CASE 3: Bytes arriving, but Framing or CRC Errors (Corrupt / Mismatched)
  if (g_stat_crc_errors > 0 || g_stat_cobs_errors > 0 || g_stat_len_errors > 0 || g_stat_delimiters == 0) {
    // Fast 5-flash strobe every 1200ms
    unsigned long p = now % 1200;
    bool on = (p < 500) && ((p % 100) < 50);
    ledcWrite(LED_BUILTIN, on ? 0 : 255);
    return;
  }

  // CASE 4: Link lost (>250ms after previously valid connection)
  unsigned long p = now % 1500;
  bool blink = (p < 80) || (p > 160 && p < 240);
  ledcWrite(LED_BUILTIN, blink ? 0 : 255);
}

// ==========================================
// Web Dashboard Handler (Accessible via Phone Browser!)
// ==========================================
void handleWebRoot() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>XIAO ESP32-C6 USB Diagnostic</title>";
  html += "<style>body{font-family:Arial,sans-serif;background:#121212;color:#eee;margin:15px;line-height:1.5}";
  html += ".card{background:#1e1e1e;border-radius:10px;padding:15px;margin-bottom:15px;border:1px solid #333}";
  html += "h1{color:#4CAF50;font-size:22px;margin:0 0 10px 0}";
  html += ".status{font-size:18px;font-weight:bold;padding:8px;border-radius:5px;display:inline-block;margin-bottom:10px}";
  html += ".ok{background:#2e7d32;color:#fff}.err{background:#c62828;color:#fff}.warn{background:#f57f17;color:#fff}";
  html += ".metric{display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid #2a2a2a}";
  html += ".label{color:#aaa}.val{font-weight:bold;color:#4fc3f7}";
  html += ".hex{background:#000;color:#00ff66;padding:8px;font-family:monospace;border-radius:5px;word-break:break-all}";
  html += "</style>";
  html += "<script>setInterval(()=>{fetch('/api/status').then(r=>r.json()).then(d=>{";
  html += "document.getElementById('raw_bytes').innerText = d.raw_bytes;";
  html += "document.getElementById('delimiters').innerText = d.delimiters;";
  html += "document.getElementById('valid_pkts').innerText = d.valid_pkts;";
  html += "document.getElementById('cobs_err').innerText = d.cobs_err;";
  html += "document.getElementById('crc_err').innerText = d.crc_err;";
  html += "document.getElementById('throttle').innerText = d.throttle + ' us (' + d.throttle_pct + '%)';";
  html += "document.getElementById('pps').innerText = d.pps;";
  html += "document.getElementById('last_err').innerText = d.last_err;";
  html += "document.getElementById('hex_dump').innerText = d.hex_dump;";
  html += "let st = document.getElementById('diag_status');";
  html += "if(d.raw_bytes == 0){st.className='status err'; st.innerText='APP SIDE ISSUE: 0 Bytes Received from USB';}";
  html += "else if(d.crc_err > 0 || d.cobs_err > 0){st.className='status warn'; st.innerText='PROTOCOL MISMATCH: Bad CRC/COBS';}";
  html += "else if(d.valid_pkts > 0 && !d.failsafe){st.className='status ok'; st.innerText='ALL SYSTEMS OK: Communicating at ' + d.pps + ' PPS';}";
  html += "else{st.className='status warn'; st.innerText='FAILSAFE: Waiting for packets';}";
  html += "});}, 500);</script></head><body>";

  html += "<h1>XIAO ESP32-C6 USB Diagnostics</h1>";
  html += "<div id='diag_status' class='status " + String(g_stat_raw_bytes == 0 ? "err" : (g_failsafe_active ? "warn" : "ok")) + "'>";
  html += String(g_stat_raw_bytes == 0 ? "APP SIDE ISSUE: 0 Bytes Received from USB" : (g_failsafe_active ? "FAILSAFE ACTIVE" : "ALL SYSTEMS OK"));
  html += "</div>";

  html += "<div class='card'>";
  html += "<div class='metric'><span class='label'>Raw USB Bytes Received:</span><span class='val' id='raw_bytes'>" + String(g_stat_raw_bytes) + "</span></div>";
  html += "<div class='metric'><span class='label'>0x00 Frame Delimiters:</span><span class='val' id='delimiters'>" + String(g_stat_delimiters) + "</span></div>";
  html += "<div class='metric'><span class='label'>Valid Setpoints (0x31):</span><span class='val' id='valid_pkts'>" + String(g_stat_valid_packets) + "</span></div>";
  html += "<div class='metric'><span class='label'>COBS Decode Errors:</span><span class='val' id='cobs_err'>" + String(g_stat_cobs_errors) + "</span></div>";
  html += "<div class='metric'><span class='label'>CRC-16 Mismatches:</span><span class='val' id='crc_err'>" + String(g_stat_crc_errors) + "</span></div>";
  html += "<div class='metric'><span class='label'>Live Throttle Level:</span><span class='val' id='throttle'>" + String(g_throttle) + " us</span></div>";
  html += "<div class='metric'><span class='label'>Packets Per Second (PPS):</span><span class='val' id='pps'>" + String(g_last_pps) + "</span></div>";
  html += "<div class='metric'><span class='label'>Last Parser State:</span><span class='val' id='last_err'>" + String(g_last_error_msg) + "</span></div>";
  html += "</div>";

  html += "<div class='card'><h3>Last Received Packet Hex:</h3><div class='hex' id='hex_dump'>" + String(g_last_hex_dump) + "</div></div>";

  html += "<div class='card'><h3>Diagnostic Guide:</h3>";
  html += "<p>• <b>If Raw Bytes = 0:</b> The Android App is NOT sending any data. Common causes: Missing USB Permission, OTG cable issue, or <code>port.setDTR(true)</code> not called in Kotlin.</p>";
  html += "<p>• <b>If Delimiters = 0:</b> App is sending bytes but not appending <code>0x00</code> delimiter.</p>";
  html += "<p>• <b>If CRC Errors > 0:</b> App CRC calculation differs from standard CRC-16/CCITT-FALSE.</p>";
  html += "</div></body></html>";

  g_server.send(200, "text/html", html);
}

void handleApiStatus() {
  float pct = ((float)(g_throttle - THROTTLE_MIN) / (float)(THROTTLE_MAX - THROTTLE_MIN)) * 100.0f;
  String json = "{";
  json += "\"raw_bytes\":" + String(g_stat_raw_bytes) + ",";
  json += "\"delimiters\":" + String(g_stat_delimiters) + ",";
  json += "\"valid_pkts\":" + String(g_stat_valid_packets) + ",";
  json += "\"cobs_err\":" + String(g_stat_cobs_errors) + ",";
  json += "\"crc_err\":" + String(g_stat_crc_errors) + ",";
  json += "\"throttle\":" + String(g_throttle) + ",";
  json += "\"throttle_pct\":" + String((int)pct) + ",";
  json += "\"pps\":" + String(g_last_pps) + ",";
  json += "\"failsafe\":" + String(g_failsafe_active ? "true" : "false") + ",";
  json += "\"last_err\":\"" + String(g_last_error_msg) + "\",";
  json += "\"hex_dump\":\"" + String(g_last_hex_dump) + "\"";
  json += "}";
  g_server.send(200, "application/json", json);
}

// ==========================================
// Setup
// ==========================================
void setup() {
  // 1. Initialize native USB CDC
  Serial.begin(115200);

  // 2. Initialize Hardware UART mirror on GPIO 16 (TX), GPIO 17 (RX)
  SerialDebug.begin(115200, SERIAL_8N1, 17, 16);
  SerialDebug.println("\n[DEBUG] XIAO ESP32-C6 Hardware UART Mirror Active");

  // 3. Initialize Built-in LED on GPIO 15 (Active-LOW PWM)
  ledcAttach(LED_BUILTIN, LED_PWM_FREQ, LED_PWM_RES);
  ledcWrite(LED_BUILTIN, 255); // Start with LED OFF

  // Startup blink sequence (3 flashes)
  for (int i = 0; i < 3; i++) {
    ledcWrite(LED_BUILTIN, 0);   delay(70);
    ledcWrite(LED_BUILTIN, 255); delay(70);
  }

  // 4. Start Wi-Fi Access Point for live browser dashboard
  WiFi.mode(WIFI_AP);
  WiFi.softAP("XIAO-C6-DEBUG", "12345678");
  SerialDebug.print("[DEBUG] Wi-Fi AP Started: XIAO-C6-DEBUG | IP: ");
  SerialDebug.println(WiFi.softAPIP());

  // 5. Setup Web Server endpoints
  g_server.on("/", handleWebRoot);
  g_server.on("/api/status", handleApiStatus);
  g_server.begin();
  SerialDebug.println("[DEBUG] Web diagnostic server started on port 80");
}

// ==========================================
// Main Loop
// ==========================================
unsigned long g_last_sec_log = 0;
unsigned long g_last_pps_calc = 0;

void loop() {
  unsigned long now = millis();

  // 1. Process USB stream from Android App
  handleUsbInput();

  // 2. Handle incoming Wi-Fi Web Dashboard requests
  g_server.handleClient();

  // 3. Airborne Failsafe Check (>250ms without packet)
  if (now - g_last_packet_time > FAILSAFE_TIMEOUT_MS) {
    if (!g_failsafe_active) {
      g_failsafe_active = true;
      g_throttle = THROTTLE_MIN; // Cutoff throttle
    }
    if (now - g_last_packet_time > 1000) {
      g_app_connected = false;
    }
  }

  // 4. Update Visual LED indicator
  updateDiagnosticLed();

  // 5. Calculate PPS every 1000ms
  if (now - g_last_pps_calc >= 1000) {
    g_last_pps_calc = now;
    uint32_t c = g_packet_counter;
    g_last_pps = (c > 255) ? 255 : (uint8_t)c;
    g_packet_counter = 0;
  }

  // 6. Echo diagnostics to Hardware UART0 (GPIO 16) once per second
  if (now - g_last_sec_log >= 1000) {
    g_last_sec_log = now;
    SerialDebug.printf("[DIAG] RawBytes: %lu | Delims: %lu | ValidPkts: %lu | CRCErr: %lu | Throttle: %u us | PPS: %u | Failsafe: %s\n",
                       g_stat_raw_bytes,
                       g_stat_delimiters,
                       g_stat_valid_packets,
                       g_stat_crc_errors,
                       g_throttle,
                       g_last_pps,
                       g_failsafe_active ? "YES" : "NO");
  }
}