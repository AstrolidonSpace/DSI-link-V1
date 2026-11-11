#pragma once

#include <Arduino.h>
#include <esp_wifi.h>

// ============================================================================
// Adaptive Frequency Hopping (AFH) & Slow Frequency Hopping (SFH) Constants
// ============================================================================

// Total number of 2.4 GHz channels evaluated (Channels 1 through 13)
#define AFH_CHANNELS_TOTAL         13

// Size of the active hopping pool (Top K cleanest channels used simultaneously)
#define AFH_ACTIVE_POOL_SIZE       4

// Dwell time: Number of 50Hz packets sent on each channel before hopping
// 5 packets @ 50 Hz (20ms interval) = 100 ms dwell time per channel (10 hops/sec)
#define AFH_DWELL_PACKETS          5
#define AFH_DWELL_PERIOD_MS        100

// Channel health evaluation & noise eviction thresholds
#define AFH_HEALTH_SMOOTH_FACTOR   0.75f  // EWMA alpha for channel score (0.0 - 1.0)
#define AFH_DEGRADED_THRESHOLD     45.0f  // Scores below 45% trigger adaptive channel replacement
#define AFH_EVAL_INTERVAL_EPOCHS   10     // Evaluate active pool health every 10 epochs (1 second)

// Receiver Synchronization Loss & Recovery Sweep timing
#define AFH_LOST_SYNC_TIMEOUT_MS   300    // Missed 3 consecutive dwells (300ms) marks sync loss
#define AFH_SEARCH_DWELL_MS        110    // Dwell per channel in Sweep Recovery mode (> 100ms)

// ============================================================================
// AFH Synchronization Header (Embedded into downlink flight control packet)
// ============================================================================
#pragma pack(push, 1)
typedef struct {
  uint16_t hop_epoch;       // Monotonically incrementing hop index
  uint8_t  current_channel; // Wi-Fi channel currently transmitting on (1..13)
  uint8_t  next_channel;    // Pre-announced next channel for zero-jitter handoff
  uint16_t active_mask;     // Bitmask of cleanest channels (bits 0..12 -> CH 1..13)
} afh_header_t;
#pragma pack(pop)

// Role definitions
enum AfhRole {
  AFH_ROLE_TX_MASTER = 1,
  AFH_ROLE_RX_SLAVE  = 2
};

// Receiver synchronization states
enum AfhSyncState {
  AFH_SYNC_LOCKED    = 1,   // In lockstep with transmitter hopping schedule
  AFH_SYNC_SEARCHING = 2    // Lost sync, performing sweep search across active pool
};

// Per-channel health and noise metrics
struct AfhChannelStats {
  uint8_t  channel;
  float    health_score;      // 0.0f (severely jammed) to 100.0f (cleanest)
  uint16_t packets_attempted;
  uint16_t packets_acked;
};

// ============================================================================
// Adaptive Frequency Hopping Manager
// ============================================================================
class AfhManager {
public:
  AfhManager();

  /**
   * Initializes the AFH engine with role and default starting channel.
   */
  void init(AfhRole role, uint8_t initial_channel = 1);

  // --------------------------------------------------------------------------
  // TRANSMITTER (TX MASTER) METHODS
  // --------------------------------------------------------------------------
  /**
   * Called on every 50Hz tick before packet transmission.
   * Advances the dwell counter, evaluates channel hops, and fills the sync header.
   * 
   * @param header Outgoing AFH sync header to embed in the radio packet.
   * @param channel_changed Set to true if the physical channel hopped this tick.
   * @param new_channel Output the newly tuned RF channel.
   */
  void master_step(afh_header_t &header, bool &channel_changed, uint8_t &new_channel);

  /**
   * Records MAC-level hardware ACK from esp_now_register_send_cb.
   */
  void master_record_ack(bool success);

  /**
   * Records telemetry feedback from receiver (e.g. PPS and RSSI).
   */
  void master_record_telemetry(uint8_t rx_channel, uint16_t pps);

  // --------------------------------------------------------------------------
  // RECEIVER (RX SLAVE) METHODS
  // --------------------------------------------------------------------------
  /**
   * Called immediately upon receiving a verified flight control packet.
   * Synchronizes epoch, receives the pre-announced next hop, and syncs active pool.
   */
  void slave_on_packet(const afh_header_t &header, bool &channel_changed, uint8_t &new_channel);

  /**
   * Called in loop() to maintain dwell timing and handle sweep search recovery.
   */
  void slave_update(bool &channel_changed, uint8_t &new_channel);

  // --------------------------------------------------------------------------
  // COMMON STATUS & DIAGNOSTICS
  // --------------------------------------------------------------------------
  uint8_t      getCurrentChannel() const { return _current_channel; }
  uint8_t      getNextChannel()    const { return _next_channel; }
  AfhSyncState getSyncState()      const { return _sync_state; }
  uint16_t     getEpoch()          const { return _current_epoch; }
  uint16_t     getActiveMask()     const { return _active_mask; }
  float        getChannelHealth(uint8_t channel) const;
  void         getActiveChannels(uint8_t out_pool[AFH_ACTIVE_POOL_SIZE]) const;

private:
  AfhRole         _role;
  AfhSyncState    _sync_state;
  uint8_t         _current_channel;
  uint8_t         _next_channel;
  uint16_t        _current_epoch;
  uint8_t         _packet_in_dwell;

  uint16_t        _active_mask;
  uint8_t         _active_pool[AFH_ACTIVE_POOL_SIZE];

  // Timing
  uint32_t        _dwell_start_time;
  uint32_t        _last_packet_time;
  uint32_t        _last_search_hop_time;
  uint8_t         _search_pool_idx;

  // Dwell statistics for noise scoring
  uint16_t        _dwell_attempted;
  uint16_t        _dwell_acked;
  AfhChannelStats _stats[AFH_CHANNELS_TOTAL];

  // Internal helpers
  uint8_t  compute_hop_channel(uint16_t epoch);
  void     evaluate_and_adapt_pool();
  void     rebuild_active_mask();
  void     unpack_active_mask(uint16_t mask);
  void     tune_radio(uint8_t channel);
};

// Global singleton instance
extern AfhManager afh_manager;

