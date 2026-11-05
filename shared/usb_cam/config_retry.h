#pragma once
#include "protocol.h"
namespace usb_cam {
// Require two fresh valid observations before changing a USB identity. A stale
// snapshot or the PID=0 status emitted during ESP startup is not a new sensor.
class ProfileDiscovery {
 public:
  bool observe(const Header& sender, uint32_t packets, UsbCamProfile active, UsbCamProfile& next) {
    if (packets == last_packets_) return false;
    last_packets_ = packets;
    if (!sender.sensor_pid) { observations_ = 0; return false; }
    const auto candidate = usb_cam_profile_for(sender.sensor_pid, sender.capabilities);
    if (candidate == active) { observations_ = 0; return false; }
    if (!observations_ || candidate != pending_) {
      pending_ = candidate; observations_ = 1; return false;
    }
    observations_ = 0;
    next = candidate;
    return true;
  }
 private:
  uint32_t last_packets_ = 0;
  unsigned observations_ = 0;
  UsbCamProfile pending_ = USB_CAM_PROFILE_DISCOVERY;
};
// Retry acknowledged startup, capture, reset and clock failures, with a finite
// budget per explicit request. Unsupported settings and a dead link stay visible.
class ConfigRetry {
 public:
  void new_request(uint32_t status_packets) {
    attempts_ = 0; waiting_ = false; last_status_ = status_packets;
  }
  bool poll(const Header& sender, uint32_t status_packets, Config& desired, uint32_t now) {
    const bool fresh = status_packets != last_status_;
    last_status_ = status_packets;
    const uint32_t token = config_token(desired);
    const bool recoverable = sender.config_error == kConfigInit || sender.config_error == kConfigFrame ||
                             sender.config_error == kConfigReset || sender.config_error == kConfigClock;
    if (sender.kind != PacketKind::Status || !recoverable ||
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
