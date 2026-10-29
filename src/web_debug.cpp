/**
 * ============================================================================
 * Module:   Transmitter Web Debug Dashboard
 * Target:   Seeed Studio XIAO ESP32-C6 / XIAO ESP32-C3
 * ============================================================================
 */

#include "web_debug.h"
#include "config.h"
#include "usb_comm.h"
#include <WiFi.h>
#include <WebServer.h>

// Web server instance listening on port 80
static WebServer s_server(80);
static bool      s_initialized = false;

// Embedded HTML / CSS / JS Single Page Application (Mobile-optimized Dark UI)
static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>DSI-Link V1 • Live Debug Dashboard</title>
  <style>
    :root {
      --bg: #0b0f19;
      --card-bg: #141c2e;
      --card-border: #1f2b45;
      --text-main: #f1f5f9;
      --text-muted: #94a3b8;
      --accent: #38bdf8;
      --accent-glow: rgba(56, 189, 248, 0.3);
      --success: #22c55e;
      --success-glow: rgba(34, 197, 94, 0.3);
      --danger: #ef4444;
      --danger-glow: rgba(239, 68, 68, 0.3);
      --warning: #f59e0b;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background-color: var(--bg);
      color: var(--text-main);
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      padding: 16px;
      line-height: 1.4;
    }
    .container { max-width: 900px; margin: 0 auto; }
    header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding-bottom: 16px;
      border-bottom: 1px solid var(--card-border);
      margin-bottom: 16px;
    }
    .brand h1 { font-size: 1.3rem; font-weight: 700; color: var(--accent); }
    .brand p { font-size: 0.8rem; color: var(--text-muted); }
    .badge {
      display: inline-flex;
      align-items: center;
      padding: 4px 10px;
      border-radius: 9999px;
      font-size: 0.75rem;
      font-weight: 700;
      letter-spacing: 0.5px;
      text-transform: uppercase;
    }
    .badge-ok { background: var(--success-glow); color: var(--success); border: 1px solid var(--success); }
    .badge-fail { background: var(--danger-glow); color: var(--danger); border: 1px solid var(--danger); }
    .badge-dot { width: 8px; height: 8px; border-radius: 50%; margin-right: 6px; }
    .dot-ok { background: var(--success); box-shadow: 0 0 8px var(--success); }
    .dot-fail { background: var(--danger); }

    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 12px; margin-bottom: 16px; }
    .card {
      background: var(--card-bg);
      border: 1px solid var(--card-border);
      border-radius: 12px;
      padding: 14px;
    }
    .card-title { font-size: 0.75rem; color: var(--text-muted); text-transform: uppercase; margin-bottom: 6px; }
    .card-value { font-size: 1.6rem; font-weight: 700; font-family: monospace; }
    .card-sub { font-size: 0.75rem; color: var(--text-muted); margin-top: 4px; }

    .section-title {
      font-size: 0.95rem;
      font-weight: 600;
      color: var(--text-muted);
      text-transform: uppercase;
      letter-spacing: 0.5px;
      margin: 18px 0 10px 0;
    }

    .channels-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
      gap: 12px;
    }
    .ch-card {
      background: var(--card-bg);
      border: 1px solid var(--card-border);
      border-radius: 10px;
      padding: 12px 14px;
    }
    .ch-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 8px;
    }
    .ch-name { font-weight: 600; font-size: 0.9rem; }
    .ch-val { font-family: monospace; font-size: 1.1rem; font-weight: 700; color: var(--accent); }
    .ch-sub { font-size: 0.75rem; color: var(--text-muted); }

    .progress-track {
      width: 100%;
      height: 10px;
      background: #1e293b;
      border-radius: 5px;
      overflow: hidden;
      position: relative;
      margin-top: 6px;
    }
    .progress-fill {
      height: 100%;
      background: linear-gradient(90deg, #38bdf8, #818cf8);
      border-radius: 5px;
      transition: width 0.08s ease;
    }
    .progress-fill-throttle {
      background: linear-gradient(90deg, #22c55e, #f59e0b, #ef4444);
    }
    .center-mark {
      position: absolute;
      top: 0;
      bottom: 0;
      left: 50%;
      width: 2px;
      background: rgba(255,255,255,0.3);
    }

    .arm-indicator {
      display: inline-block;
      padding: 3px 8px;
      border-radius: 6px;
      font-weight: 700;
      font-size: 0.8rem;
    }
    .armed { background: rgba(239, 68, 68, 0.2); color: #ef4444; border: 1px solid #ef4444; }
    .disarmed { background: rgba(148, 163, 184, 0.2); color: #94a3b8; border: 1px solid #475569; }

    footer {
      margin-top: 24px;
      text-align: center;
      font-size: 0.75rem;
      color: var(--text-muted);
      border-top: 1px solid var(--card-border);
      padding-top: 12px;
    }
  </style>
</head>
<body>
  <div class="container">
    <header>
      <div class="brand">
        <h1>DSI-Link V1</h1>
        <p>Transmitter Web Debug Dashboard</p>
      </div>
      <div id="connBadge" class="badge badge-fail">
        <span class="badge-dot dot-fail"></span>
        <span id="connText">WAITING FOR USB</span>
      </div>
    </header>

    <!-- Top KPI Grid -->
    <div class="grid">
      <div class="card">
        <div class="card-title">Link Rate (PPS)</div>
        <div class="card-value" id="valPps">0</div>
        <div class="card-sub">Packets per second</div>
      </div>
      <div class="card">
        <div class="card-title">Valid Packets</div>
        <div class="card-value" id="valValid">0</div>
        <div class="card-sub">Total frames decoded</div>
      </div>
      <div class="card">
        <div class="card-title">CRC / Decode Errors</div>
        <div class="card-value" id="valErrors" style="color:var(--danger)">0</div>
        <div class="card-sub" id="valErrorsSub">CRC: 0 | COBS: 0</div>
      </div>
      <div class="card">
        <div class="card-title">Device Uptime</div>
        <div class="card-value" id="valUptime">00:00:00</div>
        <div class="card-sub" id="valBytes">0 bytes rx</div>
      </div>
    </div>

    <!-- Primary Flight Controls -->
    <div class="section-title">Primary Flight Channels</div>
    <div class="channels-grid">
      <!-- Throttle -->
      <div class="ch-card">
        <div class="ch-header">
          <div>
            <div class="ch-name">CH3: Throttle</div>
            <div class="ch-sub" id="thPct">0%</div>
          </div>
          <div class="ch-val" id="valCh3">1000 µs</div>
        </div>
        <div class="progress-track">
          <div class="progress-fill progress-fill-throttle" id="barCh3" style="width: 0%"></div>
        </div>
      </div>

      <!-- Roll -->
      <div class="ch-card">
        <div class="ch-header">
          <div>
            <div class="ch-name">CH1: Roll (Aileron)</div>
            <div class="ch-sub" id="rollSub">Center: 1500</div>
          </div>
          <div class="ch-val" id="valCh1">1500 µs</div>
        </div>
        <div class="progress-track">
          <div class="center-mark"></div>
          <div class="progress-fill" id="barCh1" style="width: 50%"></div>
        </div>
      </div>

      <!-- Pitch -->
      <div class="ch-card">
        <div class="ch-header">
          <div>
            <div class="ch-name">CH2: Pitch (Elevator)</div>
            <div class="ch-sub" id="pitchSub">Center: 1500</div>
          </div>
          <div class="ch-val" id="valCh2">1500 µs</div>
        </div>
        <div class="progress-track">
          <div class="center-mark"></div>
          <div class="progress-fill" id="barCh2" style="width: 50%"></div>
        </div>
      </div>

      <!-- Yaw -->
      <div class="ch-card">
        <div class="ch-header">
          <div>
            <div class="ch-name">CH4: Yaw (Rudder)</div>
            <div class="ch-sub" id="yawSub">Center: 1500</div>
          </div>
          <div class="ch-val" id="valCh4">1500 µs</div>
        </div>
        <div class="progress-track">
          <div class="center-mark"></div>
          <div class="progress-fill" id="barCh4" style="width: 50%"></div>
        </div>
      </div>
    </div>

    <!-- Auxiliary Channels -->
    <div class="section-title">Auxiliary Controls</div>
    <div class="channels-grid">
      <!-- AUX 1 (Arm Switch) -->
      <div class="ch-card">
        <div class="ch-header">
          <div>
            <div class="ch-name">CH5: AUX 1 (Arm Switch)</div>
            <div class="ch-sub"><span id="armBadge" class="arm-indicator disarmed">DISARMED</span></div>
          </div>
          <div class="ch-val" id="valCh5">1000 µs</div>
        </div>
        <div class="progress-track">
          <div class="progress-fill" id="barCh5" style="width: 0%"></div>
        </div>
      </div>

      <!-- AUX 2 -->
      <div class="ch-card">
        <div class="ch-header">
          <div>
            <div class="ch-name">CH6: AUX 2 (Mode Switch)</div>
            <div class="ch-sub" id="aux2Sub">Mode: 0 / 7</div>
          </div>
          <div class="ch-val" id="valCh6">1000 µs</div>
        </div>
        <div class="progress-track">
          <div class="progress-fill" id="barCh6" style="width: 0%"></div>
        </div>
      </div>

      <!-- AUX 3 -->
      <div class="ch-card">
        <div class="ch-header">
          <div>
            <div class="ch-name">CH7: AUX 3</div>
            <div class="ch-sub" id="aux3Sub">Level: 0 / 7</div>
          </div>
          <div class="ch-val" id="valCh7">1000 µs</div>
        </div>
        <div class="progress-track">
          <div class="progress-fill" id="barCh7" style="width: 0%"></div>
        </div>
      </div>

      <!-- AUX 4 -->
      <div class="ch-card">
        <div class="ch-header">
          <div>
            <div class="ch-name">CH8: AUX 4</div>
            <div class="ch-sub" id="aux4Sub">Level: 0 / 7</div>
          </div>
          <div class="ch-val" id="valCh8">1000 µs</div>
        </div>
        <div class="progress-track">
          <div class="progress-fill" id="barCh8" style="width: 0%"></div>
        </div>
      </div>

      <!-- AUX 5 -->
      <div class="ch-card">
        <div class="ch-header">
          <div>
            <div class="ch-name">CH9: AUX 5 (Auxiliary 5)</div>
            <div class="ch-sub" id="aux5Sub">Raw: 0 / 63</div>
          </div>
          <div class="ch-val" id="valCh9">1000 µs</div>
        </div>
        <div class="progress-track">
          <div class="progress-fill" id="barCh9" style="width: 0%"></div>
        </div>
      </div>
    </div>

    <footer>
      DSI-Link V1 Firmware • AstrolidonSpace Proprietary Product • Auto-refresh active (10 Hz)
    </footer>
  </div>

  <script>
    function fmtUptime(sec) {
      const h = Math.floor(sec / 3600).toString().padStart(2, '0');
      const m = Math.floor((sec % 3600) / 60).toString().padStart(2, '0');
      const s = Math.floor(sec % 60).toString().padStart(2, '0');
      return `${h}:${m}:${s}`;
    }

    function usToPct(us) {
      const clamped = Math.min(Math.max(us, 1000), 2000);
      return ((clamped - 1000) / 1000) * 100;
    }

    async function updateDashboard() {
      try {
        const res = await fetch('/api/status');
        if (!res.ok) throw new Error("HTTP error " + res.status);
        const d = await res.json();

        // 1. Connection Status Badge
        const badge = document.getElementById('connBadge');
        const text  = document.getElementById('connText');
        if (d.connected && !d.failsafe) {
          badge.className = 'badge badge-ok';
          badge.querySelector('.badge-dot').className = 'badge-dot dot-ok';
          text.textContent = 'CONNECTED (' + d.pps + ' PPS)';
        } else {
          badge.className = 'badge badge-fail';
          badge.querySelector('.badge-dot').className = 'badge-dot dot-fail';
          text.textContent = d.failsafe ? 'FAILSAFE (TIMEOUT)' : 'DISCONNECTED';
        }

        // 2. Metrics Grid
        document.getElementById('valPps').textContent = d.pps;
        document.getElementById('valValid').textContent = d.valid.toLocaleString();
        document.getElementById('valErrors').textContent = (d.crc_err + d.cobs_err).toLocaleString();
        document.getElementById('valErrorsSub').textContent = `CRC: ${d.crc_err} | COBS: ${d.cobs_err}`;
        document.getElementById('valUptime').textContent = fmtUptime(d.uptime);
        document.getElementById('valBytes').textContent = `${d.raw_bytes.toLocaleString()} bytes rx`;

        // 3. Flight Channels
        // Roll (CH1)
        const ch1 = d.ch[0];
        document.getElementById('valCh1').textContent = `${ch1} µs`;
        document.getElementById('barCh1').style.width = `${usToPct(ch1)}%`;

        // Pitch (CH2)
        const ch2 = d.ch[1];
        document.getElementById('valCh2').textContent = `${ch2} µs`;
        document.getElementById('barCh2').style.width = `${usToPct(ch2)}%`;

        // Throttle (CH3)
        const ch3 = d.ch[2];
        document.getElementById('valCh3').textContent = `${ch3} µs`;
        document.getElementById('thPct').textContent = `${d.throttle_pct}%`;
        document.getElementById('barCh3').style.width = `${d.throttle_pct}%`;

        // Yaw (CH4)
        const ch4 = d.ch[3];
        document.getElementById('valCh4').textContent = `${ch4} µs`;
        document.getElementById('barCh4').style.width = `${usToPct(ch4)}%`;

        // 4. Auxiliary Channels
        // CH5 (AUX 1 - Arm)
        const ch5 = d.ch[4];
        document.getElementById('valCh5').textContent = `${ch5} µs`;
        document.getElementById('barCh5').style.width = `${usToPct(ch5)}%`;
        const armBadge = document.getElementById('armBadge');
        if (d.aux_raw[0] === 1) {
          armBadge.className = 'arm-indicator armed';
          armBadge.textContent = 'ARMED';
        } else {
          armBadge.className = 'arm-indicator disarmed';
          armBadge.textContent = 'DISARMED';
        }

        // CH6 (AUX 2)
        const ch6 = d.ch[5];
        document.getElementById('valCh6').textContent = `${ch6} µs`;
        document.getElementById('barCh6').style.width = `${usToPct(ch6)}%`;
        document.getElementById('aux2Sub').textContent = `Mode: ${d.aux_raw[1]} / 7`;

        // CH7 (AUX 3)
        const ch7 = d.ch[6];
        document.getElementById('valCh7').textContent = `${ch7} µs`;
        document.getElementById('barCh7').style.width = `${usToPct(ch7)}%`;
        document.getElementById('aux3Sub').textContent = `Level: ${d.aux_raw[2]} / 7`;

        // CH8 (AUX 4)
        const ch8 = d.ch[7];
        document.getElementById('valCh8').textContent = `${ch8} µs`;
        document.getElementById('barCh8').style.width = `${usToPct(ch8)}%`;
        document.getElementById('aux4Sub').textContent = `Level: ${d.aux_raw[3]} / 7`;

        // CH9 (AUX 5)
        const ch9 = d.ch[8];
        document.getElementById('valCh9').textContent = `${ch9} µs`;
        document.getElementById('barCh9').style.width = `${usToPct(ch9)}%`;
        document.getElementById('aux5Sub').textContent = `Raw: ${d.aux_raw[4]} / 63`;

      } catch (e) {
        const badge = document.getElementById('connBadge');
        badge.className = 'badge badge-fail';
        document.getElementById('connText').textContent = 'RECONNECTING...';
      }
    }

    // Refresh at 10 Hz (every 100ms)
    setInterval(updateDashboard, 100);
    updateDashboard();
  </script>
</body>
</html>)rawliteral";

// ============================================================================
// HTTP Handlers
// ============================================================================

static void handleRoot() {
  s_server.send(200, "text/html", INDEX_HTML);
}

static void handleApiStatus() {
  // Compute percentage for throttle (0 - 100%)
  uint16_t clamped_th = constrain(usb_throttle, THROTTLE_MIN, THROTTLE_MAX);
  uint8_t  th_pct     = (uint8_t)(((clamped_th - THROTTLE_MIN) * 100UL) / (THROTTLE_MAX - THROTTLE_MIN));

  // Decode PWM values for AUX channels
  uint16_t aux1_pwm = usb_aux1 ? 2000 : 1000;
  uint16_t aux2_pwm = (usb_aux2 == 0) ? 1000 : ((usb_aux2 == 1 || usb_aux2 == 7) ? 2000 : map(usb_aux2, 0, 7, 1000, 2000));
  uint16_t aux3_pwm = (usb_aux3 == 0) ? 1000 : ((usb_aux3 == 1 || usb_aux3 == 7) ? 2000 : map(usb_aux3, 0, 7, 1000, 2000));
  uint16_t aux4_pwm = (usb_aux4 == 0) ? 1000 : ((usb_aux4 == 1 || usb_aux4 == 7) ? 2000 : map(usb_aux4, 0, 7, 1000, 2000));
  uint16_t aux5_pwm = map(usb_aux5, 0, 63, 1000, 2000);

  uint32_t uptime_sec = millis() / 1000UL;

  // Build JSON response
  String json = "{";
  json += "\"connected\":" + String(usb_connected ? "true" : "false") + ",";
  json += "\"failsafe\":" + String(usb_input.failsafe ? "true" : "false") + ",";
  json += "\"pps\":" + String(usb_input.pps) + ",";
  json += "\"valid\":" + String(usb_stats.valid_packets) + ",";
  json += "\"crc_err\":" + String(usb_stats.crc_errors) + ",";
  json += "\"cobs_err\":" + String(usb_stats.cobs_errors) + ",";
  json += "\"raw_bytes\":" + String(usb_stats.raw_bytes) + ",";
  json += "\"uptime\":" + String(uptime_sec) + ",";
  json += "\"throttle_pct\":" + String(th_pct) + ",";
  json += "\"ch\":[" + 
          String(usb_roll) + "," + 
          String(usb_pitch) + "," + 
          String(usb_throttle) + "," + 
          String(usb_yaw) + "," + 
          String(aux1_pwm) + "," + 
          String(aux2_pwm) + "," + 
          String(aux3_pwm) + "," + 
          String(aux4_pwm) + "," + 
          String(aux5_pwm) + "],";
  json += "\"aux_raw\":[" + 
          String(usb_aux1) + "," + 
          String(usb_aux2) + "," + 
          String(usb_aux3) + "," + 
          String(usb_aux4) + "," + 
          String(usb_aux5) + "]";
  json += "}";

  s_server.send(200, "application/json", json);
}

// ============================================================================
// Public API Implementation
// ============================================================================

bool web_debug_init() {
  // 1. Initialize Wi-Fi in SoftAP mode
  WiFi.mode(WIFI_AP);
  bool ap_ok = WiFi.softAP(TX_WEB_SSID, TX_WEB_PASSWORD);
  
  if (!ap_ok) {
    Serial.println("[WEB_DEBUG] Failed to start Wi-Fi SoftAP!");
    return false;
  }

  Serial.println();
  Serial.println("==================================================");
  Serial.print(" [WEB_DEBUG] SoftAP Started: ");
  Serial.println(TX_WEB_SSID);
  Serial.print(" [WEB_DEBUG] IP Address:     http://");
  Serial.println(WiFi.softAPIP());
  Serial.println("==================================================");
  Serial.println();

  // 2. Register HTTP endpoints
  s_server.on("/", handleRoot);
  s_server.on("/api/status", handleApiStatus);

  // 3. Start Web Server
  s_server.begin();
  s_initialized = true;
  return true;
}

void web_debug_update() {
  if (s_initialized) {
    s_server.handleClient();
  }
}

