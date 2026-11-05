#pragma once
#include <cstdint>

namespace usb_cam {
// Unsigned elapsed-time arithmetic also works after a long inactive stream.
// A signed future-deadline comparison would stall after ~35 minutes of idle.
class FrameClock {
 public:
  explicit FrameClock(uint32_t interval_us) : interval_us_(interval_us) {}
  bool due(uint32_t now_us) const { return uint32_t(now_us - last_us_) >= interval_us_; }
  void sent(uint32_t now_us) { last_us_ = now_us; }

 private:
  uint32_t interval_us_;
  uint32_t last_us_ = 0;
};
}  // namespace usb_cam
