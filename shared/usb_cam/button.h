#pragma once
#include <array>
#include "wire.h"

namespace usb_cam {
constexpr uint32_t kButtonCapability = 4;
constexpr uint32_t kButtonSequenceMask = 0x7fffffffu;
constexpr uint32_t kButtonResync = 0x80000000u;
constexpr uint32_t kButtonAckMagic = 0x314e5442; // BTN1
constexpr uint32_t kButtonDebounceMs = 50;
struct ButtonEvent {
  uint32_t boot_id = 0, sequence = 0, timestamp_ms = 0; // boot_id is a resync session ID.
  bool pressed = false;
  constexpr ButtonEvent(uint32_t session = 0, uint32_t count = 0, uint32_t time = 0, bool down = false)
      : boot_id(session), sequence(count), timestamp_ms(time), pressed(down) {}
};
struct ButtonAck {
  uint32_t boot_id, sequence;
  constexpr ButtonAck(uint32_t session = 0, uint32_t count = 0) : boot_id(session), sequence(count) {}
};
inline uint32_t button_next_sequence(uint32_t sequence) { return (sequence + 1) & kButtonSequenceMask; }

// Fixed FIFO, externally synchronized when shared between cores/tasks.
template <typename T, size_t Capacity> class ButtonQueue {
 public:
  bool push(const T& value) {
    if (size_ == Capacity) return false;
    values_[(head_ + size_) % Capacity] = value; ++size_; return true;
  }
  const T* peek() const { return size_ ? &values_[head_] : nullptr; }
  void pop() { if (size_) { head_ = (head_ + 1) % Capacity; --size_; } }
  void clear() { head_ = size_ = 0; }
  size_t size() const { return size_; }
 private:
  std::array<T, Capacity> values_{};
  size_t head_ = 0, size_ = 0;
};

class ButtonSource {
 public:
  void begin(uint32_t session) {
    *this = ButtonSource{}; latest_.boot_id = session;
  }
  void sample(bool physical_pressed, uint32_t now_ms) {
    if (physical_pressed != physical_) { physical_ = physical_pressed; changed_at_ = now_ms; }
    if (uint32_t(now_ms - changed_at_) < kButtonDebounceMs || latest_.pressed == physical_) return;
    latest_.pressed = physical_;
    latest_.sequence = button_next_sequence(latest_.sequence);
    latest_.timestamp_ms = now_ms;
    if (!events_.push(latest_)) {
      lost_ += uint32_t(events_.size()); events_.clear(); events_.push(latest_);
    }
  }
  ButtonEvent snapshot() const { const auto* e = events_.peek(); return e ? *e : latest_; }
  bool acknowledge(ButtonAck ack) {
    const auto* e = events_.peek();
    if (!e || ack.boot_id != e->boot_id || ack.sequence != e->sequence) return false;
    events_.pop(); return true;
  }
  bool resynchronize(uint32_t expected_session, uint32_t new_session, uint32_t now_ms) {
    if (expected_session != latest_.boot_id || !new_session || new_session == expected_session) return false;
    events_.clear(); latest_.boot_id = new_session; latest_.sequence = 0;
    latest_.timestamp_ms = now_ms; return true; // Keep both debounced and physical state.
  }
  size_t pending() const { return events_.size(); }
  uint32_t lost() const { return lost_; }
 private:
  ButtonEvent latest_;
  bool physical_ = false;
  uint32_t changed_at_ = 0, lost_ = 0;
  ButtonQueue<ButtonEvent,32> events_;
};

// Optional transport sideband in CFG2. These bytes are never part of Config.
inline bool valid_button_ack_bytes(const uint8_t* data) {
  bool empty = true;
  for (size_t i = 102; i < 114; ++i) empty = empty && data[i] == 0;
  if (!empty && (get32(data+102) != kButtonAckMagic || !get32(data+106) ||
      get32(data+110) > kButtonResync)) return false;
  for (size_t i = 114; i < 124; ++i) if (data[i]) return false;
  return true;
}
inline void encode_button_ack(ButtonAck ack, uint8_t* data) {
  if (!ack.boot_id) return;
  put32(data+102,kButtonAckMagic); put32(data+106,ack.boot_id); put32(data+110,ack.sequence);
  put32(data+124,crc32(data,124));
}
// Caller must have validated the complete CFG2 packet and CRC first.
inline ButtonAck decode_button_ack(const uint8_t* data) {
  return get32(data+102) == kButtonAckMagic ? ButtonAck{get32(data+106),get32(data+110)} : ButtonAck{};
}
} // namespace usb_cam
