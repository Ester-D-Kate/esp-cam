#pragma once
#include <esp_camera.h>
#include "usb_cam/config_session.h"
#include "usb_cam/protocol.h"

class CameraControl {
 public:
  void configure(const usb_cam::Config& config);
  camera_fb_t* capture(usb_cam::Header& counters, size_t& length);
  void describe(usb_cam::Header& header) const;
  bool can_capture() const { return running_ && session_.can_capture(); }
  uint32_t interval_us() const { return session_.desired().interval_100ns / 10; }
 private:
  usb_cam::ConfigSession session_;
  bool running_ = false;
  uint8_t quality_ = 16, discard_ = 0, bad_geometry_ = 0;
  uint16_t sensor_pid_ = 0;
};
