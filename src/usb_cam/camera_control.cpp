#include "camera_control.h"
#include "sensor_settings.h"
#include <Arduino.h>
#define CAMERA_MODEL_AI_THINKER
#include "../camera_pins.h"

void CameraControl::configure(const usb_cam::Config& desired) {
  if (!session_.request(desired)) return;
  // Called only after returning the last framebuffer. Reinitialize to make
  // unspecified controls mean sensor defaults, including after partial failure.
  if (running_) {
    if (esp_camera_deinit() != ESP_OK) { session_.fail(usb_cam::kConfigInit); return; }
    running_ = false;
  }
  if (!psramFound()) { session_.fail(usb_cam::kConfigInit); return; }
  camera_config_t config{};
  config.ledc_channel = LEDC_CHANNEL_0; config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = desired.xclk_mhz * 1000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_UXGA; // Reserve buffers for every supported mode.
  config.jpeg_quality = desired.quality;
  config.fb_count = 2; config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;
  if (esp_camera_init(&config) != ESP_OK) { session_.fail(usb_cam::kConfigInit); return; }
  running_ = true;
  sensor_t* sensor = esp_camera_sensor_get();
  sensor_pid_ = sensor ? sensor->id.PID : 0;
  const uint32_t error = usb_cam::apply_sensor_config(sensor, desired);
  if (error) { session_.fail(error); return; }
  quality_ = desired.quality; discard_ = 3; bad_geometry_ = 0;
  Serial.printf("Config generation=%lu sensor=0x%04x %ux%u interval=%lu xclk=%u MHz\n",
      (unsigned long)desired.generation, sensor_pid_, desired.width, desired.height,
      (unsigned long)desired.interval_100ns, desired.xclk_mhz);
}

camera_fb_t* CameraControl::capture(usb_cam::Header& counters, size_t& length) {
  if (!can_capture()) return nullptr;
  camera_fb_t* frame = esp_camera_fb_get();
  ++counters.sequence;
  if (!frame) { ++counters.capture_errors; return nullptr; }
  if (discard_) { --discard_; esp_camera_fb_return(frame); return nullptr; }
  const usb_cam::Config& config = session_.desired();
  length = frame->len;
  if (frame->format != PIXFORMAT_JPEG ||
      !usb_cam::validate_jpeg(frame->buf, length, config.width, config.height)) {
    ++counters.capture_errors;
    if (++bad_geometry_ >= 8) session_.fail(usb_cam::kConfigFrame);
    esp_camera_fb_return(frame); return nullptr;
  }
  bad_geometry_ = 0;
  // Confirm only a real, correctly sized JPEG. Oversize is a transport limit,
  // reported separately, and must not block receiving the next command.
  session_.confirm(config.width, config.height);
  if (length > config.max_jpeg) {
    ++counters.oversized;
    if (quality_ < 50) {
      const uint8_t quality = quality_ < 48 ? quality_ + 2 : 50;
      sensor_t* sensor = esp_camera_sensor_get();
      if (!sensor || !sensor->set_quality || sensor->set_quality(sensor, quality))
        session_.fail(usb_cam::kConfigQuality);
      else quality_ = quality;
    }
    esp_camera_fb_return(frame); return nullptr;
  }
  return frame;
}

void CameraControl::describe(usb_cam::Header& h) const {
  const auto& c = session_.desired();
  h.width = c.width; h.height = c.height; h.interval_100ns = c.interval_100ns;
  h.generation = session_.applied_generation(); h.config_token = session_.applied_token();
  h.attempted_generation = c.generation; h.attempted_token = usb_cam::config_token(c);
  h.config_error = session_.error(); h.quality = quality_; h.sensor_pid = sensor_pid_;
}
