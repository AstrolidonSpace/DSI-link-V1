# DSI Link V1: Long-Range ESP-NOW 50Hz RC & Telemetry System

[![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-orange?logo=platformio)](https://platformio.org/)
[![ESP32](https://img.shields.io/badge/ESP32-C6%20%7C%20C3%20%7C%20DevModule-blue?logo=espressif)](https://www.espressif.com/)
[![Rate](https://img.shields.io/badge/Link%20Rate-50Hz%20Deterministic-brightgreen)](#)
[![AFH-SFH](https://img.shields.io/badge/AFH--SFH-10%20Hops%2Fs%20(Adaptive)-blueviolet)](#adaptive-slow-frequency-hopping-afh-sfh)
[![Telemetry](https://img.shields.io/badge/Telemetry-10Hz%20Uplink-green)](#7-byte-uplink-telemetry-packet-10hz)
[![PWM](https://img.shields.io/badge/PWM%20Output-9--CH%2012--Bit%20%40%2050Hz-purple)](#)
[![Product](https://img.shields.io/badge/AstrolidonSpace-Proprietary%20Product-critical)](#)

A high-performance, deterministic 50Hz wireless flight control and 10Hz telemetry bridge built for RC aircraft, multirotors, and robotics. DSI Link V1 connects an Android Ground Control Station (GCS) or mobile transmitter to an airborne receiver with ultra-low latency, hardware failsafes, **Adaptive Slow Frequency Hopping (AFH-SFH)** for dynamic 2.4 GHz interference evasion, and bidirectional data link using ESP-NOW in Long-Range (LR) mode.

---

## 📑 Table of Contents

- [System Architecture](#system-architecture)
- [Key Features](#key-features)
- [Adaptive Slow Frequency Hopping (AFH-SFH)](#adaptive-slow-frequency-hopping-afh-sfh)
  - [Hopping Principles & Dwell Time](#1-hopping-principles--dwell-time)
  - [Zero-Jitter In-Band Next-Hop Announcement](#2-zero-jitter-in-band-next-hop-announcement)
  - [Dynamic Noise Eviction & EWMA Scoring](#3-dynamic-noise-eviction--ewma-scoring)
  - [Fast Sweep Recovery Mode](#4-fast-sweep-recovery-mode)
- [Hardware & Pinout Diagrams](#hardware--pinout-diagrams)
  - [Transmitter (TX)](#1-transmitter-tx)
  - [Receiver (RX)](#2-receiver-rx)
- [Protocol Specifications](#protocol-specifications)
  - [22-Byte Downlink Control Frame (50Hz)](#22-byte-downlink-control-frame-50hz)
  - [AUX Channel Bitfield Mapping](#aux-channel-bitfield-mapping-icd-spec-rev-24)
  - [7-Byte Uplink Telemetry Packet (10Hz)](#7-byte-uplink-telemetry-packet-10hz)
- [Configuration & Tuning](#configuration--tuning)
- [How to Build & Flash](#how-to-build--flash)
  - [Prerequisites](#prerequisites)
  - [Flashing the Transmitter (TX)](#flashing-the-transmitter-tx)
  - [Flashing the Receiver (RX)](#flashing-the-receiver-rx)
  - [Dual-Role Configuration](#switching-roles-tx-vs-rx)
  - [Transmitter Operation Mode (Transmit vs. Live Web Debug)](#transmitter-operation-mode-transmit-vs-live-web-debug)
- [Pairing & First-Time Setup](#pairing--first-time-setup)
- [Failsafe & Safety Behavior](#failsafe--safety-behavior)
- [Troubleshooting & Diagnostics](#troubleshooting--diagnostics)
- [Copyright & Proprietary Notice](#copyright--proprietary-notice)

---

## System Architecture

```text
┌─────────────────────────────────┐
│     Android Smartphone / App    │  Custom GCS / Controller
└───────────────┬─────────────────┘
                │ USB-CDC (OTG Cable, Full-Duplex, COBS + CRC-16)
                ▼
┌─────────────────────────────────┐
│    Transmitter (TX) Hardware    │  Seeed Studio XIAO ESP32-C6 / ESP32-C3
│  - FreeRTOS 50Hz Task           │  - Uninterrupted USB Telemetry Streaming
│  - ESP-NOW Long Range (LR)      │  - CRC-16 Calculation & Heartbeat
│  - AFH Master (Adaptive Hopping)│  - Hardware MAC ACK Loss Tracking
└───────────────┬─────────────────┘
                │
                │ 📡 2.4 GHz ESP-NOW Long Range (LR Mode) with AFH-SFH
                │    Downlink: 50 Hz Control Frames (22 Bytes with AFH Sync Header)
                │    Uplink:   10 Hz Telemetry Packets (7 Bytes with Health & Channel)
                │    Hop Rate: 10 Hops/sec across 4 cleanest channels (100ms dwell)
                ▼
┌─────────────────────────────────┐
│     Receiver (RX) Hardware      │  ESP32 Dev Module
│  - 9x 12-Bit LEDC PWM @ 50Hz    │  - Hardware Failsafe Watchdog (500ms)
│  - Connection LED (GPIO 2)      │  - Analog Battery ADC Monitor (GPIO 34)
│  - AFH Slave (Auto Lock & Sweep)│  - Rapid Resync Recovery (< 400ms)
└───────────────┬─────────────────┘
                │ 9x Standard 50Hz RC PWM Signals (1000 - 2000 µs)
                ▼
┌─────────────────────────────────────────────────────────────┐
│ Servos, ESCs, Flight Controller (Betaflight / INAV / Ardu)  │
└─────────────────────────────────────────────────────────────┘
```

---

## Key Features

- **Strict 50 Hz Deterministic Loop**: Transmitter uses FreeRTOS `vTaskDelayUntil()` to eliminate timing jitter, ensuring exact 20 ms transmission intervals.
- **Adaptive Slow Frequency Hopping (AFH-SFH)**: Hops at 10 hops/sec (100 ms dwell) across the 4 cleanest channels in the 2.4 GHz band, actively evading Wi-Fi, Bluetooth, and microwave interference.
- **Hardware-Level Noise & ACK Tracking**: Tracks 802.11 MAC-level hardware ACKs via `esp_now_register_send_cb` to evaluate per-channel Packet Delivery Ratio (PDR) with an Exponential Weighted Moving Average (EWMA) filter.
- **Dynamic Jammer Eviction**: If any channel in the active pool degrades ($< 45\%$ health score), the master automatically evicts it and replaces it with the cleanest available channel, broadcasting the new mask to the receiver.
- **Zero-Desync Next-Hop Pre-announcement**: Every frame pre-announces `next_channel`. Catching even 1 packet out of 5 during a dwell guarantees a synchronized channel transition.
- **Fast Sweep Resynchronization**: If the receiver loses contact for $> 300\text{ ms}$, it performs a 110 ms dwell sweep across active channels, re-locking phase in under $400\text{ ms}$.
- **ESP-NOW Long Range (LR) Mode**: Configures lower PHY data rates for increased receiver sensitivity and significantly longer line-of-sight range.
- **10 Hz Telemetry Uplink**: Receiver samples battery voltage, calculates 1-second rolling PPS, and reports current RF channel back to the transmitter every 100 ms.
- **Full-Duplex / Collision-Free USB**: TX streams telemetry data back to the Android mobile app concurrently without interrupting incoming control packets.
- **9-Channel 12-Bit PWM Generation**: Standard 50 Hz RC PWM output (20 ms period) on ESP32 Dev Module with 12-bit hardware timer resolution (0.24 µs resolution per count).
- **Auto-Pairing**: Receiver automatically discovers and binds to the transmitter's MAC address upon receiving the first valid packet.
- **Comprehensive Multi-Tier Failsafe**: If no valid packet arrives for $> 500\text{ ms}$, throttle drops immediately to 1000 µs (cutoff), flight axes return to center (1500 µs), switches disarm (1000 µs), and GPIO 2 turns OFF.

---

## Adaptive Slow Frequency Hopping (AFH-SFH)

### 1. Hopping Principles & Dwell Time
In **Slow Frequency Hopping (SFH)**, the dwell time spans multiple packets. DSI Link V1 transmits at 50 Hz (20 ms per packet) and configures:
- **Dwell Size**: 5 packets per channel.
- **Dwell Time**: $5 \times 20\text{ ms} = 100\text{ ms}$ per channel ($10\text{ hops/second}$).
- **Overhead**: Tuning the ESP32 Wi-Fi radio via `esp_wifi_set_channel()` takes $\sim 1\text{--}2\text{ ms}$. Over a 100 ms dwell, radio synthesizer switching accounts for $< 2\%$ overhead, maximizing usable airtime.

### 2. Zero-Jitter In-Band Next-Hop Announcement
To eliminate the desynchronization risks common in standard FHSS systems, every transmitted frame carries an `afh_header_t`:
```c
typedef struct {
  uint16_t hop_epoch;       // Monotonic hop counter
  uint8_t  current_channel; // Channel currently transmitting on (1..13)
  uint8_t  next_channel;    // Target channel for the next dwell epoch
  uint16_t active_mask;     // 16-bit bitmask of clean active channels
} afh_header_t;
```
Because the transmitter announces `next_channel` continuously across all 5 packets in the current dwell:
- Even if 4 out of 5 packets are jammed or lost, **a single captured packet is sufficient** for the receiver to transition synchronously to the next channel.
- Both transmitter and receiver tune to `next_channel` simultaneously when the 100 ms dwell expires.

### 3. Dynamic Noise Eviction & EWMA Scoring
Channels are continuously scored on a scale from $0.0\%$ (heavily jammed) to $100.0\%$ (clean):
1. **Per-Dwell MAC Feedback**: During transmission, the transmitter registers `esp_now_register_send_cb` to count hardware ACKs from the receiver.
2. **EWMA Smoothing**:
   $$Q_{ch}(k) = 0.75 \cdot Q_{ch}(k-1) + 0.25 \cdot \text{DwellAckRate}$$
3. **Adaptive Replacement**: Every 10 epochs (1 second), channels in the active pool with $Q_{ch} < 45\%$ are marked degraded. The master scans unselected channels (from the 13 available 2.4 GHz channels), selects the candidate with the highest health score, and updates `active_mask`.
4. **Gradual Recovery**: Evicted channels gently recover their score over time ($+1.5\%$ per eval cycle), ensuring temporary local interference does not banish a channel permanently.

```text
               Active Hopping Pool (Top 4 Clean Channels)
             ┌──────────┬──────────┬──────────┬──────────┐
             │ Channel 1│ Channel 5│ Channel 9│Channel 13│
             └──────────┴─────┬────┴──────────┴──────────┘
                              │
                    Health drops < 45%
                 (Wi-Fi Router / Microwave)
                              ▼
                        [EVICT CH 5]
                              │
                    Select Cleanest Spare
                    (e.g., Channel 11: 98%)
                              ▼
             ┌──────────┬──────────┬──────────┬──────────┐
             │ Channel 1│Channel 11│ Channel 9│Channel 13│
             └──────────┴──────────┴──────────┴──────────┘
```

### 4. Fast Sweep Recovery Mode
If the receiver experiences an extended loss of signal ($> 300\text{ ms}$ / 3 missed dwell cycles):
1. RX transitions from `AFH_SYNC_LOCKED` to `AFH_SYNC_SEARCHING`.
2. RX dwells on each active channel for **110 ms** (intentionally slightly longer than the TX dwell of 100 ms).
3. Because the transmitter continuously cycles through all 4 active channels within $400\text{ ms}$, it is guaranteed to land on the receiver's listening channel within at most $400\text{ ms}$.
4. The instant any valid frame is received, the receiver extracts `hop_epoch` and `next_channel`, immediately re-locks phase (`AFH_SYNC_LOCKED`), and resumes lockstep hopping.

---

## Hardware & Pinout Diagrams

### 1. Transmitter (TX)

| Component | Target Microcontroller | Notes |
| :--- | :--- | :--- |
| **Board** | **Seeed Studio XIAO ESP32-C6** or **XIAO ESP32-C3** | Ultra-compact form factor |
| **USB Port** | Native USB-C | Connects directly to Android phone via USB-C to USB-C OTG cable |
| **Status LED** | Onboard LED | Fast blink on boot, active upon USB handshake |

> [!TIP]
> The transmitter is powered directly by the smartphone through the USB-C OTG connection. No external battery is required on the TX side.

---

### 2. Receiver (RX)

The receiver runs on a standard **ESP32 Dev Module** (30-pin or 38-pin version). It outputs 9 independent PWM channels, monitors flight battery voltage, and provides a hardware link status LED.

#### RX Pinout Table

| Channel | Function | Signal Pin (GPIO) | Pulse Range | Neutral / Failsafe |
| :--- | :--- | :--- | :--- | :--- |
| **CH 1** | **Roll** (Aileron) | **GPIO 13** | 1000 – 2000 µs | 1500 µs (Center) |
| **CH 2** | **Pitch** (Elevator) | **GPIO 14** | 1000 – 2000 µs | 1500 µs (Center) |
| **CH 3** | **Throttle** | **GPIO 27** | 1000 – 2000 µs | **1000 µs (Cutoff)** |
| **CH 4** | **Yaw** (Rudder) | **GPIO 26** | 1000 – 2000 µs | 1500 µs (Center) |
| **CH 5** | **AUX 1 (Arm Switch)** | **GPIO 25** | 1000 / 2000 µs | **1000 µs (Disarmed)** |
| **CH 6** | **AUX 2 (Flight Mode)**| **GPIO 33** | 1000 – 2000 µs (8 levels)| 1000 µs |
| **CH 7** | **AUX 3** | **GPIO 32** | 1000 – 2000 µs (8 levels)| 1000 µs |
| **CH 8** | **AUX 4** | **GPIO 4**  | 1000 – 2000 µs (8 levels)| 1000 µs |
| **CH 9** | **AUX 5** | **GPIO 18** | 1000 – 2000 µs (64 levels)| 1000 µs |
| **STATUS**| **Link Status LED** | **GPIO 2** | **HIGH = Connected, LOW = Failsafe** | Onboard Blue LED |
| **BATT** | **Battery Voltage ADC**| **GPIO 34** | 0 – 3.3V (Divider to LiPo) | Analog Input |
| **VIN**  | **Power Supply (5V)**  | **5V / VIN** | 5.0V from BEC or ESC | Power In |
| **GND**  | **Common Ground**      | **GND**      | Common ground with servos/ESC | Ground |

```text
                  ESP32 Dev Module (Receiver Pinout)
                            ┌──────────────┐
                     3V3 ───┤ 1         30 ├──── GND
                     EN  ───┤ 2         29 ├──── GPIO 23
         ADC / BATT (34) ───┤ 3         28 ├──── GPIO 22
                     35  ───┤ 4         27 ├──── GPIO 1  (TX0)
                     32  ───┤ 5         26 ├──── GPIO 3  (RX0)
                     33  ───┤ 6         25 ├──── GPIO 21
  AUX 1 [Arm]    (25)───┤ 7         24 ├──── GPIO 19
  Yaw   [CH4]    (26)───┤ 8         23 ├──── GPIO 18 ─── AUX 5   [CH9]
  Throttle [CH3] (27)───┤ 9         22 ├──── GPIO 5
  Pitch [CH2]    (14)───┤ 10        21 ├──── GPIO 17
  Roll  [CH1]    (13)───┤ 11        20 ├──── GPIO 16
  AUX 4 [CH8]    (4) ───┤ 12        19 ├──── GPIO 4
  Status LED     (2) ───┤ 13        18 ├──── GPIO 0
                     15  ───┤ 14        17 ├──── GPIO 2   ─── Link Status LED
                    GND  ───┤ 15        16 ├──── 5V / VIN ─── 5V from BEC
                            └──────────────┘
```

#### Battery Voltage Divider Circuit (Optional on GPIO 34)

To monitor a 1S–4S LiPo battery, wire a resistive divider to GPIO 34:
```text
LiPo (+) ──[ 10kΩ ]──┬──[ 2.7kΩ ]── GND
                     │
                     └──> GPIO 34 (ADC1_CH6)
```
*(Default divider multiplier in [`include/config.h`](file:///c:/Users/dipan/OneDrive/Dokumen/PlatformIO/Projects/DSI%20link%20test/include/config.h) is `4.65f`).*

---

## Protocol Specifications

### 22-Byte Downlink Control Frame (50Hz)

```c
#pragma pack(push, 1)
typedef struct {
  uint32_t     packet_id;    // 4 Bytes: Incrementing sequence counter
  uint16_t     roll;         // 2 Bytes: Channel 1 (1000 - 2000 µs, 1500 = Center)
  uint16_t     pitch;        // 2 Bytes: Channel 2 (1000 - 2000 µs, 1500 = Center)
  uint16_t     yaw;          // 2 Bytes: Channel 3 (1000 - 2000 µs, 1500 = Center)
  uint16_t     throttle;     // 2 Bytes: Channel 4 (1000 - 2000 µs, 1000 = Cutoff)
  uint16_t     aux_channels; // 2 Bytes: Packed 16-bit auxiliary bitfield
  afh_header_t afh;          // 6 Bytes: Adaptive Frequency Hopping metadata
  uint16_t     crc;          // 2 Bytes: CRC-16-CCITT over the preceding 20 bytes
} flight_control_packet_t;
#pragma pack(pop)
```

### AUX Channel Bitfield Mapping (ICD SPEC-REV 2.4)

The 16-bit `aux_channels` integer packs all 5 auxiliary controls:

| Field | Bit Range | Width | States / Resolution | Output PWM (µs) |
| :--- | :--- | :--- | :--- | :--- |
| **AUX 1 (Arm)** | `Bit 0` | 1 bit | `0` = Disarmed, `1` = Armed | 1000 µs / 2000 µs |
| **AUX 2** | `Bits 1..3` | 3 bits | 0 to 7 (8 discrete positions) | 1000 to 2000 µs |
| **AUX 3** | `Bits 4..6` | 3 bits | 0 to 7 (8 discrete positions) | 1000 to 2000 µs |
| **AUX 4** | `Bits 7..9` | 3 bits | 0 to 7 (8 discrete positions) | 1000 to 2000 µs |
| **AUX 5** | `Bits 10..15`| 6 bits | 0 to 63 (64 discrete levels) | 1000 to 2000 µs |

### 7-Byte Uplink Telemetry Packet (10Hz)

Sent back from Receiver to Transmitter every 100 ms:

```c
#pragma pack(push, 1)
typedef struct {
  uint8_t  packet_id;          // 1 Byte: Telemetry packet type ID
  uint16_t packet_count_500ms; // 2 Bytes: Packets received in previous 500ms window
  uint16_t battery_mV;         // 2 Bytes: Measured battery voltage in millivolts
  uint8_t  rx_channel;         // 1 Byte: Radio channel RX is currently tuned to
  uint8_t  rx_rssi;            // 1 Byte: Reserved link quality / RSSI indicator
} telemetry_packet_t;
#pragma pack(pop)
```

---

## Configuration & Tuning

Key system parameters can be configured in [`include/config.h`](file:///c:/Users/dipan/OneDrive/Dokumen/PlatformIO/Projects/DSI%20link%20test/include/config.h) and [`include/afh_link.h`](file:///c:/Users/dipan/OneDrive/Dokumen/PlatformIO/Projects/DSI%20link%20test/include/afh_link.h):

### In `include/config.h`:
```c
// Enable/disable Adaptive Frequency Hopping (1 = Active, 0 = Static Single Channel)
#define AFH_ENABLED            1

// Initial bootup channel (1 to 13)
#define AFH_INITIAL_CHANNEL    1
```

### In `include/afh_link.h`:
```c
#define AFH_CHANNELS_TOTAL         13    // Total 2.4 GHz channels evaluated (1..13)
#define AFH_ACTIVE_POOL_SIZE       4     // Number of active hopping channels
#define AFH_DWELL_PACKETS          5     // 5 pkts @ 50 Hz = 100 ms dwell
#define AFH_HEALTH_SMOOTH_FACTOR   0.75f // EWMA alpha for channel health smoothing
#define AFH_DEGRADED_THRESHOLD     45.0f // Health score threshold triggering eviction
#define AFH_LOST_SYNC_TIMEOUT_MS   300   // 3 missed dwells triggers sweep search
#define AFH_SEARCH_DWELL_MS        110   // Dwell per channel during sweep recovery
```

---

## How to Build & Flash

### Prerequisites

1. Install [VS Code](https://code.visualstudio.com/) and the **PlatformIO IDE** extension.
2. Clone this repository:
   ```bash
   git clone https://github.com/AstrolidonSpace/DSI-link-V1.git
   cd DSI-link-V1
   ```

---

### Flashing the Transmitter (TX)

Connect your XIAO ESP32-C6 (or XIAO ESP32-C3) to your PC with a USB-C data cable.

#### Using PlatformIO CLI:
```bash
# For Seeed Studio XIAO ESP32-C6:
pio run -e seeed-xiao-esp32-c6 -t upload

# For Seeed Studio XIAO ESP32-C3:
pio run -e seeed_xiao_esp32c3 -t upload
```

#### Using VS Code GUI:
1. Open the PlatformIO tab on the left sidebar.
2. Under **Project Tasks**, expand **seeed-xiao-esp32-c6** (or `seeed_xiao_esp32c3`).
3. Click **Upload**.

---

### Flashing the Receiver (RX)

Connect your ESP32 Dev Module to your PC via Micro-USB / USB-C.

#### Using PlatformIO CLI:
```bash
pio run -e rx-esp32dev -t upload
```

#### Using VS Code GUI:
1. Open the PlatformIO tab on the left sidebar.
2. Under **Project Tasks**, expand **rx-esp32dev**.
3. Click **Upload**.

---

### Switching Roles (TX vs RX)

The codebase automatically selects the role based on the active PlatformIO environment:
- Environment `rx-esp32dev` compiles as **Receiver** (`-D ROLE_RECEIVER`).
- Environments `seeed-xiao-esp32-c6` and `seeed_xiao_esp32c3` compile as **Transmitter** (`-D ROLE_TRANSMITTER`).

If compiling outside of PlatformIO (e.g. Arduino IDE), configure the role in [`include/config.h`](file:///c:/Users/dipan/OneDrive/Dokumen/PlatformIO/Projects/DSI%20link%20test/include/config.h):
```c
// Toggle one of these:
#define ROLE_TRANSMITTER
//#define ROLE_RECEIVER
```

### Transmitter Operation Mode (Transmit vs. Live Web Debug)

In [`include/config.h`](file:///c:/Users/dipan/OneDrive/Dokumen/PlatformIO/Projects/DSI%20link%20test/include/config.h), you can toggle between flight transmission and live web diagnostics:

```c
// Select ONE mode:
#define TX_OPERATION_MODE  TX_MODE_TRANSMIT   // Standard 50Hz ESP-NOW LR wireless flight link
//#define TX_OPERATION_MODE  TX_MODE_WEB_DEBUG // Live Wi-Fi AP + Web Dashboard at http://192.168.4.1
```

#### Web Debug Dashboard Features
- **Wi-Fi AP:** `DSI-LINK-DEBUG` (Password: `12345678`)
- **Dashboard URL:** `http://192.168.4.1`
- **Real-time Live Channels:** CH1 (Roll), CH2 (Pitch), CH3 (Throttle % & µs), CH4 (Yaw), CH5 (Arm state), CH6–CH9 (AUX 2–5).
- **Diagnostics:** Live PPS meter, connection status badge (Connected vs Failsafe), total valid frames, and CRC/COBS error tracking auto-refreshing at 10 Hz.

---

## Pairing & First-Time Setup

1. **Power up the Receiver first**:
   - Open the Serial Monitor (`115200` baud).
   - Upon boot, the RX displays its MAC address and initial AFH state:
     ```text
     ==================================================
      [RX] MAC Address: 80:B5:4E:C1:10:EC
      [RX] C-Array: {0x80, 0xB5, 0x4E, 0xC1, 0x10, 0xEC}
     ==================================================
     [AFH] Initialized in RECEIVER (Slave) Mode
     [AFH] Active Pool: { 1, 5, 9, 13 }, Starting CH: 1
     ```
2. **Set Peer MAC in Transmitter** (Optional):
   - In [`include/espnow_link.h`](file:///c:/Users/dipan/OneDrive/Dokumen/PlatformIO/Projects/DSI%20link%20test/include/espnow_link.h), set `DEFAULT_DRONE_MAC` to your RX's MAC address if you want a fixed 1-to-1 link.
   - Alternatively, the receiver automatically captures the transmitter's MAC on first transmission and begins returning telemetry immediately.
3. **Connect to Phone**:
   - Plug the TX into your Android device.
   - Launch your GCS / Controller App.
   - The RX onboard LED (GPIO 2) will turn solid **HIGH**, indicating an active 50Hz control link with live frequency hopping.

---

## Failsafe & Safety Behavior

Safety is built into every layer of DSI Link:

1. **USB Disconnection on TX**: If the mobile device stops feeding frames for $> 250\text{ ms}$, the TX immediately transmits failsafe frames with Throttle = 1000 µs and Disarm (`aux_channels = 0`).
2. **Radio Link Loss on RX**: If RF reception ceases for $> 500\text{ ms}$:
   - **Throttle (CH3)** cuts immediately to **1000 µs**.
   - **Flight Controls (CH1, CH2, CH4)** return to center **1500 µs**.
   - **Arm Switch (CH5)** drops to **1000 µs (Disarmed)**.
   - **GPIO 2 Status LED** turns **LOW**.
3. **Data Integrity (CRC-16)**: Every control frame carries a 16-bit CRC checksum. Corrupted, partial, or malformed packets are instantly discarded before affecting PWM outputs or hopping state.
4. **Resilient Hopping Continuity**: Even under severe localized RF packet loss, the pre-announced `next_channel` mechanism ensures transmitter and receiver stay in lockstep without losing synchronization.

---

## Troubleshooting & Diagnostics

| Symptom | Cause | Solution |
| :--- | :--- | :--- |
| **RX Status LED (GPIO 2) remains OFF** | No ESP-NOW packets arriving | Verify TX has power and is receiving commands over USB. Check that TX is sending to the correct peer MAC. |
| **Frequent Sweep Recovery logs on RX** | Extreme interference on active channels | Ensure antennas are oriented perpendicularly. AFH will automatically adapt to cleaner channels within seconds. |
| **Servos jittering or unresponsive** | Shared power supply sagging | Power servos from a dedicated 5V BEC rather than the ESP32 3.3V rail. Connect grounds together. |
| **Telemetry not appearing on Android App** | Baud rate mismatch or USB-OTG permission | Ensure OTG is enabled in Android settings and app has permission to access USB device. |
| **Compiler error: `intelhex` not found** | PlatformIO environment dependency | Run `pip install intelhex` in PlatformIO penv or terminal. |

---

## Copyright & Proprietary Notice

**Copyright © 2026 AstrolidonSpace. All Rights Reserved.**

This firmware, protocol specification, and documentation constitute proprietary intellectual property of **AstrolidonSpace**. This is a commercial product codebase. Unauthorized reproduction, distribution, reverse engineering, or unauthorized commercial deployment of this software without explicit written permission from AstrolidonSpace is strictly prohibited.
