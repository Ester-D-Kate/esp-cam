#pragma once

#include <cassert>
#include <cstdint>

namespace usb_cam {
enum class SlotState : uint8_t { Free, Receiving, Received, Validating, Ready, Sending };

// No platform locking here: the caller protects every operation with one lock.
// Data is accessed without the lock only while owned by Receiving/Validating/Sending.
class FrameSlots {
 public:
  static constexpr int kCount = 2;

  int begin_receive(bool& replaced) {
    replaced = false;
    int slot = find(SlotState::Free);
    // With two slots, reclaiming Ready while the other is Sending discards the
    // only complete next frame before the next CS even arrives. Apply READY
    // backpressure instead; USB completion frees its slot without losing this one.
    if (slot < 0 && find(SlotState::Sending) < 0) {
      slot = oldest_ready();
      replaced = slot >= 0;
    }
    if (slot >= 0) states_[slot] = SlotState::Receiving;
    return slot;
  }

  void received(int slot) { transition(slot, SlotState::Receiving, SlotState::Received); }
  int begin_validate() {
    const int slot = find(SlotState::Received);
    if (slot >= 0) states_[slot] = SlotState::Validating;
    return slot;
  }
  void publish(int slot) {
    transition(slot, SlotState::Validating, SlotState::Ready);
    ages_[slot] = ++generation_;
  }
  int take_latest(uint32_t* discarded = nullptr) {
    int latest = -1;
    for (int i = 0; i < kCount; ++i) {
      if (states_[i] == SlotState::Ready &&
          (latest < 0 || int32_t(ages_[i] - ages_[latest]) > 0)) latest = i;
    }
    if (latest >= 0) states_[latest] = SlotState::Sending;
    for (int i = 0; i < kCount; ++i) {
      if (states_[i] == SlotState::Ready) {
        states_[i] = SlotState::Free;
        if (discarded) ++*discarded;
      }
    }
    return latest;
  }
  void release(int slot) {
    assert(slot >= 0 && slot < kCount);
    states_[slot] = SlotState::Free;
  }
  SlotState state(int slot) const { return states_[slot]; }

 private:
  void transition(int slot, SlotState from, SlotState to) {
    assert(slot >= 0 && slot < kCount && states_[slot] == from);
    (void)from;
    states_[slot] = to;
  }
  int find(SlotState state) const {
    for (int i = 0; i < kCount; ++i) if (states_[i] == state) return i;
    return -1;
  }
  int oldest_ready() const {
    int oldest = -1;
    for (int i = 0; i < kCount; ++i) {
      if (states_[i] == SlotState::Ready &&
          (oldest < 0 || int32_t(ages_[i] - ages_[oldest]) < 0)) oldest = i;
    }
    return oldest;
  }
  SlotState states_[kCount]{};
  uint32_t ages_[kCount]{};
  uint32_t generation_ = 0;
};
}  // namespace usb_cam
