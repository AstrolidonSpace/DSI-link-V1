/**
 * ============================================================================
 * Module:   Adaptive Frequency Hopping (AFH) & Slow Frequency Hopping (SFH)
 * Target:   ESP32 / ESP32-C3 / ESP32-C6 (DSI Link Control & Telemetry)
 * ============================================================================
 */

#include "afh_link.h"

// Global singleton instance
AfhManager afh_manager;

AfhManager::AfhManager()
  : _role(AFH_ROLE_TX_MASTER),
    _sync_state(AFH_SYNC_LOCKED),
    _current_channel(1),
    _next_channel(5),
    _current_epoch(0),
    _packet_in_dwell(0),
    _active_mask(0),
    _dwell_start_time(0),
    _last_packet_time(0),
    _last_search_hop_time(0),
    _search_pool_idx(0),
    _dwell_attempted(0),
    _dwell_acked(0)
{
  // Initialize all 13 channel statistics to full health (100%)
  for (uint8_t i = 0; i < AFH_CHANNELS_TOTAL; i++) {
    _stats[i].channel           = i + 1;
    _stats[i].health_score      = 100.0f;
    _stats[i].packets_attempted = 0;
    _stats[i].packets_acked     = 0;
  }

  // Default initial active pool: 20 MHz spaced channels (1, 5, 9, 13)
  _active_pool[0] = 1;
  _active_pool[1] = 5;
  _active_pool[2] = 9;
  _active_pool[3] = 13;

  rebuild_active_mask();
}

void AfhManager::init(AfhRole role, uint8_t initial_channel) {
  _role = role;

  if (initial_channel < 1 || initial_channel > AFH_CHANNELS_TOTAL) {
    initial_channel = 1;
  }

  _current_channel      = initial_channel;
  _current_epoch        = 0;
  _packet_in_dwell      = 0;
  _dwell_start_time     = millis();
  _last_packet_time     = millis();
  _last_search_hop_time = millis();
  _sync_state           = AFH_SYNC_LOCKED;
  _search_pool_idx      = 0;
  _dwell_attempted      = 0;
  _dwell_acked          = 0;

  // Initial active hopping pool: Channels 1, 5, 9, 13
  _active_pool[0] = 1;
  _active_pool[1] = 5;
  _active_pool[2] = 9;
  _active_pool[3] = 13;
  rebuild_active_mask();

  _next_channel = compute_hop_channel(_current_epoch + 1);

  // Directly tune the baseband synthesizer
  tune_radio(_current_channel);

  Serial.println("==================================================");
  Serial.printf("[AFH] Initialized in %s Mode\n", (_role == AFH_ROLE_TX_MASTER) ? "TRANSMITTER (Master)" : "RECEIVER (Slave)");
  Serial.printf("[AFH] Active Pool: { %d, %d, %d, %d }, Starting CH: %d\n",
                _active_pool[0], _active_pool[1], _active_pool[2], _active_pool[3], _current_channel);
  Serial.printf("[AFH] Dwell: %d pkts (%d ms/hop), Total Channels Evaluated: %d\n",
                AFH_DWELL_PACKETS, AFH_DWELL_PERIOD_MS, AFH_CHANNELS_TOTAL);
  Serial.println("==================================================");
}

void AfhManager::tune_radio(uint8_t channel) {
  if (channel < 1 || channel > AFH_CHANNELS_TOTAL) {
    channel = 1;
  }
  _current_channel = channel;
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

// Deterministic Pseudo-Random Permutation generator for hop sequence
uint8_t AfhManager::compute_hop_channel(uint16_t epoch) {
  uint16_t cycle = epoch / AFH_ACTIVE_POOL_SIZE;
  uint8_t  slot  = epoch % AFH_ACTIVE_POOL_SIZE;

  // Pseudo-random permutation of indices 0..3 for this cycle
  uint8_t perm[AFH_ACTIVE_POOL_SIZE] = {0, 1, 2, 3};
  uint32_t h = ((uint32_t)cycle ^ 0xA5A5) * 1103515245UL + 12345UL;

  for (int i = AFH_ACTIVE_POOL_SIZE - 1; i > 0; i--) {
    int j = (int)((h >> (i * 4)) % (uint32_t)(i + 1));
    uint8_t tmp = perm[i];
    perm[i]     = perm[j];
    perm[j]     = tmp;
  }

  uint8_t pool_idx = perm[slot];
  return _active_pool[pool_idx];
}

void AfhManager::master_step(afh_header_t &header, bool &channel_changed, uint8_t &new_channel) {
  channel_changed = false;
  _packet_in_dwell++;
  _dwell_attempted++;

  // End of dwell window reached (e.g. 5 packets @ 50Hz = 100ms)
  if (_packet_in_dwell >= AFH_DWELL_PACKETS) {
    // 1. Update EWMA health score for the channel we just operated on
    if (_dwell_attempted > 0) {
      float dwell_ack_rate = ((float)_dwell_acked / (float)_dwell_attempted) * 100.0f;
      uint8_t ch_idx = _current_channel - 1;
      if (ch_idx < AFH_CHANNELS_TOTAL) {
        _stats[ch_idx].packets_attempted += _dwell_attempted;
        _stats[ch_idx].packets_acked     += _dwell_acked;
        _stats[ch_idx].health_score      = (AFH_HEALTH_SMOOTH_FACTOR * _stats[ch_idx].health_score) +
                                           ((1.0f - AFH_HEALTH_SMOOTH_FACTOR) * dwell_ack_rate);
      }
    }

    _dwell_attempted = 0;
    _dwell_acked     = 0;
    _current_epoch++;

    // 2. Periodically re-evaluate channel health and evict jammed/noisy channels
    if (_current_epoch % AFH_EVAL_INTERVAL_EPOCHS == 0) {
      evaluate_and_adapt_pool();
    }

    // 3. Hop to the pre-announced next channel
    _current_channel = _next_channel;
    _next_channel    = compute_hop_channel(_current_epoch + 1);

    tune_radio(_current_channel);
    channel_changed  = true;
    new_channel      = _current_channel;
    _packet_in_dwell = 0;
  }

  // Populate outgoing sync header for the receiver
  header.hop_epoch       = _current_epoch;
  header.current_channel = _current_channel;
  header.next_channel    = _next_channel;
  header.active_mask     = _active_mask;
}

void AfhManager::master_record_ack(bool success) {
  if (success) {
    _dwell_acked++;
  }
}

void AfhManager::master_record_telemetry(uint8_t rx_channel, uint16_t pps) {
  (void)rx_channel;
  // If telemetry packets arrive with solid PPS, reinforce health score
  if (pps > 10) {
    uint8_t idx = _current_channel - 1;
    if (idx < AFH_CHANNELS_TOTAL) {
      _stats[idx].health_score = (0.9f * _stats[idx].health_score) + (0.1f * 100.0f);
    }
  }
}

void AfhManager::evaluate_and_adapt_pool() {
  // Check if any channel currently active in the pool has degraded below threshold
  for (uint8_t i = 0; i < AFH_ACTIVE_POOL_SIZE; i++) {
    uint8_t active_ch = _active_pool[i];
    uint8_t ch_idx    = active_ch - 1;

    if (_stats[ch_idx].health_score < AFH_DEGRADED_THRESHOLD) {
      // Degraded channel found! Search candidate channels outside active pool
      uint8_t best_candidate = 0;
      float   highest_score  = -1.0f;

      for (uint8_t c = 1; c <= AFH_CHANNELS_TOTAL; c++) {
        // Must not already be in active pool
        bool in_pool = false;
        for (uint8_t k = 0; k < AFH_ACTIVE_POOL_SIZE; k++) {
          if (_active_pool[k] == c) {
            in_pool = true;
            break;
          }
        }
        if (!in_pool) {
          float score = _stats[c - 1].health_score;
          if (score > highest_score) {
            highest_score  = score;
            best_candidate = c;
          }
        }
      }

      // If a viable candidate exists, swap out the noisy channel
      if (best_candidate != 0 && highest_score > _stats[ch_idx].health_score + 10.0f) {
        Serial.printf("[AFH] Noise/Jamming detected on CH %d (Health: %.1f%%). Evicted -> Switched to clean CH %d (Health: %.1f%%)\n",
                      active_ch, _stats[ch_idx].health_score, best_candidate, highest_score);

        _active_pool[i] = best_candidate;
        rebuild_active_mask();

        // If the next planned channel was the evicted one, recompute immediately
        if (_next_channel == active_ch) {
          _next_channel = best_candidate;
        }
      }
    }
  }

  // Gently allow non-active channels to recover their health score over time
  // so temporary localized Wi-Fi bursts don't banish a channel forever
  for (uint8_t c = 0; c < AFH_CHANNELS_TOTAL; c++) {
    bool in_pool = false;
    for (uint8_t k = 0; k < AFH_ACTIVE_POOL_SIZE; k++) {
      if (_active_pool[k] == (c + 1)) {
        in_pool = true;
        break;
      }
    }
    if (!in_pool && _stats[c].health_score < 85.0f) {
      _stats[c].health_score += 1.5f;
    }
  }
}

void AfhManager::slave_on_packet(const afh_header_t &header, bool &channel_changed, uint8_t &new_channel) {
  channel_changed   = false;
  _last_packet_time = millis();

  // 1. If RX was previously lost/searching, instantly re-lock!
  if (_sync_state == AFH_SYNC_SEARCHING) {
    _sync_state        = AFH_SYNC_LOCKED;
    _current_epoch     = header.hop_epoch;
    _current_channel   = header.current_channel;
    _next_channel      = header.next_channel;
    _dwell_start_time  = millis();
    _packet_in_dwell   = 1;

    unpack_active_mask(header.active_mask);
    tune_radio(_current_channel);

    channel_changed = true;
    new_channel     = _current_channel;

    Serial.printf("[AFH-RX] Intercepted Master on CH %d! Locked to Epoch %u. Next CH: %d\n",
                  _current_channel, _current_epoch, _next_channel);
    return;
  }

  // 2. Normal locked operation
  _current_epoch = header.hop_epoch;
  _next_channel  = header.next_channel;
  unpack_active_mask(header.active_mask);

  _packet_in_dwell++;

  // When all dwell packets arrive, synchronously step to next pre-announced channel
  if (_packet_in_dwell >= AFH_DWELL_PACKETS) {
    _current_channel  = _next_channel;
    tune_radio(_current_channel);

    _dwell_start_time = millis();
    _current_epoch++;
    _next_channel     = compute_hop_channel(_current_epoch + 1);
    _packet_in_dwell  = 0;

    channel_changed   = true;
    new_channel       = _current_channel;
  }
}

void AfhManager::slave_update(bool &channel_changed, uint8_t &new_channel) {
  uint32_t now    = millis();
  channel_changed = false;

  // --------------------------------------------------------------------------
  // STATE 1: SYNC_LOCKED (Normal in-phase hopping)
  // --------------------------------------------------------------------------
  if (_sync_state == AFH_SYNC_LOCKED) {
    // Check if link dropped for > 300ms (3 dwell periods)
    if (now - _last_packet_time > AFH_LOST_SYNC_TIMEOUT_MS) {
      _sync_state           = AFH_SYNC_SEARCHING;
      _search_pool_idx      = 0;
      _last_search_hop_time = now;
      _current_channel      = _active_pool[_search_pool_idx];

      tune_radio(_current_channel);
      channel_changed = true;
      new_channel     = _current_channel;

      Serial.printf("[AFH-RX] Link timeout (>%d ms). Entering Sweep Search on CH %d...\n",
                    AFH_LOST_SYNC_TIMEOUT_MS, _current_channel);
      return;
    }

    // Time-based dwell timeout trigger:
    // If packets were lost during the dwell, hop to the pre-announced next channel
    // when the 100ms dwell window expires, preventing desynchronization.
    if (now - _dwell_start_time >= AFH_DWELL_PERIOD_MS) {
      _current_channel  = _next_channel;
      tune_radio(_current_channel);

      _dwell_start_time = now;
      _current_epoch++;
      _next_channel     = compute_hop_channel(_current_epoch + 1);
      _packet_in_dwell  = 0;

      channel_changed   = true;
      new_channel       = _current_channel;
    }
  }
  // --------------------------------------------------------------------------
  // STATE 2: SYNC_SEARCHING (Sweep across active pool to re-acquire Master)
  // --------------------------------------------------------------------------
  else if (_sync_state == AFH_SYNC_SEARCHING) {
    // Dwell on each candidate channel for 110ms (slightly longer than master's 100ms)
    // Master is cycling all 4 channels in 400ms, guaranteed to hit this channel!
    if (now - _last_search_hop_time >= AFH_SEARCH_DWELL_MS) {
      _last_search_hop_time = now;
      _search_pool_idx      = (_search_pool_idx + 1) % AFH_ACTIVE_POOL_SIZE;
      _current_channel      = _active_pool[_search_pool_idx];

      tune_radio(_current_channel);
      channel_changed = true;
      new_channel     = _current_channel;
    }
  }
}

void AfhManager::rebuild_active_mask() {
  uint16_t mask = 0;
  for (uint8_t i = 0; i < AFH_ACTIVE_POOL_SIZE; i++) {
    uint8_t ch = _active_pool[i];
    if (ch >= 1 && ch <= AFH_CHANNELS_TOTAL) {
      mask |= (1 << (ch - 1));
    }
  }
  _active_mask = mask;
}

void AfhManager::unpack_active_mask(uint16_t mask) {
  if (mask == _active_mask || mask == 0) return;

  uint8_t new_pool[AFH_ACTIVE_POOL_SIZE];
  uint8_t count = 0;

  for (uint8_t ch = 1; ch <= AFH_CHANNELS_TOTAL && count < AFH_ACTIVE_POOL_SIZE; ch++) {
    if (mask & (1 << (ch - 1))) {
      new_pool[count++] = ch;
    }
  }

  // Only apply if exactly AFH_ACTIVE_POOL_SIZE channels were extracted
  if (count == AFH_ACTIVE_POOL_SIZE) {
    _active_mask = mask;
    for (uint8_t i = 0; i < AFH_ACTIVE_POOL_SIZE; i++) {
      _active_pool[i] = new_pool[i];
    }
  }
}

float AfhManager::getChannelHealth(uint8_t channel) const {
  if (channel >= 1 && channel <= AFH_CHANNELS_TOTAL) {
    return _stats[channel - 1].health_score;
  }
  return 0.0f;
}

void AfhManager::getActiveChannels(uint8_t out_pool[AFH_ACTIVE_POOL_SIZE]) const {
  for (uint8_t i = 0; i < AFH_ACTIVE_POOL_SIZE; i++) {
    out_pool[i] = _active_pool[i];
  }
}

