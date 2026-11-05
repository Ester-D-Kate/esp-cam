#pragma once
#include "configuration.h"
namespace usb_cam {
// A failed request cannot publish images. It remains command-responsive and
// waits for a changed desired state instead of endlessly retrying sensor I/O.
class ConfigSession {
 public:
  bool request(const Config& config) {
    if (!valid_config(config) || (have_request_ && config_equal(config, desired_))) return false;
    desired_ = config; have_request_ = true; active_ = false; error_ = 0;
    return true;
  }
  void fail(uint32_t error) { error_ = error; active_ = false; }
  bool confirm(uint16_t width, uint16_t height) {
    if (!can_capture() || width != desired_.width || height != desired_.height) return false;
    active_ = true; applied_generation_ = desired_.generation;
    applied_token_ = config_token(desired_); return true;
  }
  bool active() const { return active_; }
  bool can_capture() const { return have_request_ && !error_; }
  uint32_t error() const { return error_; }
  uint32_t applied_generation() const { return applied_generation_; }
  uint32_t applied_token() const { return applied_token_; }
  const Config& desired() const { return desired_; }
 private:
  Config desired_;
  bool have_request_ = false, active_ = false;
  uint32_t error_ = 0, applied_generation_ = 0, applied_token_ = 0;
};
} // namespace usb_cam
