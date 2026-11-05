#pragma once
#include "protocol.h"
namespace usb_cam {
// Retry only acknowledged initialization failures, with a finite budget per
// explicit request. Other errors and an unresponsive link stay visible.
class ConfigRetry {
 public:
  void new_request(uint32_t status_packets) {
    attempts_ = 0; waiting_ = false; last_status_ = status_packets;
  }
  bool poll(const Header& sender, uint32_t status_packets, Config& desired, uint32_t now) {
    const bool fresh = status_packets != last_status_;
    last_status_ = status_packets;
    const uint32_t token = config_token(desired);
    if (sender.kind != PacketKind::Status || sender.config_error != kConfigInit ||
        sender.attempted_generation != desired.generation || sender.attempted_token != token) {
      waiting_ = false;
      return false;
    }
    if (!fresh) return false;
    if (attempts_ >= 2) return false;
    if (!waiting_ || generation_ != desired.generation || token_ != token) {
      waiting_ = true; since_ = now; generation_ = desired.generation; token_ = token;
      return false;
    }
    if (uint32_t(now - since_) < 250000) return false;
    ++desired.generation;
    ++attempts_;
    waiting_ = false;
    return true;
  }
  unsigned attempts() const { return attempts_; }
 private:
  uint32_t last_status_ = 0, since_ = 0, generation_ = 0, token_ = 0;
  unsigned attempts_ = 0;
  bool waiting_ = false;
};
} // namespace usb_cam
