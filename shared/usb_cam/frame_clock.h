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
  void interval(uint32_t interval_us) { interval_us_ = interval_us; }

 private:
  uint32_t interval_us_;
  uint32_t last_us_ = 0;
};

// Maximum inter-frame gap since the previous report, retaining the previous
// timestamp across reports. Reset on a new stream to exclude inactive time.
class FrameGap {
 public:
  void observe(uint32_t now_us) {
    const uint32_t gap = now_us - last_us_;
    if (has_previous_ && gap > maximum_us_) maximum_us_ = gap;
    last_us_ = now_us;
    has_previous_ = true;
  }
  uint32_t take_max() {
    const uint32_t result = maximum_us_;
    maximum_us_ = 0;
    return result;
  }
  void reset() { has_previous_ = false; maximum_us_ = 0; }

 private:
  uint32_t last_us_ = 0, maximum_us_ = 0;
  bool has_previous_ = false;
};
}  // namespace usb_cam
