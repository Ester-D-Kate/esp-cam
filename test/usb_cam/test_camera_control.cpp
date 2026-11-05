#include <cassert>
#include <cstdio>
#include <vector>
#include <Arduino.h>
#include "camera_control.h"
#include "sensor_settings.h"

using namespace usb_cam;
TestSerial Serial;
static bool psram = true, init_ok = true, capture_ok = true, held = false;
static bool quality_ok = true;
static unsigned starts = 0, returns = 0;
static sensor_t sensor;
static camera_fb_t frame;
static std::vector<uint8_t> jpeg;
bool psramFound() { return psram; }
static int quality(sensor_t*, int) { return quality_ok ? 0 : -1; }
static int brightness_fail(sensor_t*, int) { return -1; }
static int size(sensor_t*, framesize_t value) {
  if (value != FRAMESIZE_VGA && value != FRAMESIZE_SVGA) return -1;
  const unsigned w = value == FRAMESIZE_VGA ? 640 : 800;
  const unsigned h = value == FRAMESIZE_VGA ? 480 : 600;
  jpeg = {0xff,0xd8,0xff,0xc0,0,11,8,uint8_t(h>>8),uint8_t(h),uint8_t(w>>8),uint8_t(w),
          1,1,0x11,0,0xff,0xda,0,8,1,1,0,0,63,0,0x42,0xff,0xd9};
  frame = {}; frame.buf = jpeg.data(); frame.len = jpeg.size(); frame.width = w; frame.height = h;
  frame.format = PIXFORMAT_JPEG;
  return 0;
}
extern "C" esp_err_t esp_camera_init(const camera_config_t* config) {
  assert(!held && config->frame_size == FRAMESIZE_UXGA && config->fb_count == 2);
  assert(config->xclk_freq_hz == 20000000 || config->xclk_freq_hz == 10000000);
  assert(config->pin_d0 == 5 && config->pin_xclk == 0 && config->pin_sccb_sda == 26);
  ++starts;
  return init_ok ? ESP_OK : ESP_FAIL;
}
extern "C" esp_err_t esp_camera_deinit() { assert(!held); return ESP_OK; }
extern "C" sensor_t* esp_camera_sensor_get() { return &sensor; }
extern "C" camera_fb_t* esp_camera_fb_get() {
  assert(!held);
  if (!capture_ok) return nullptr;
  held = true; return &frame;
}
extern "C" void esp_camera_fb_return(camera_fb_t* returned) {
  assert(held && returned == &frame); held = false; ++returns;
}
static void warmup(CameraControl& camera, Header& status) {
  size_t length = 0;
  for (unsigned i = 0; i < 3; ++i) assert(!camera.capture(status, length) && !held);
  camera.describe(status);
  assert(status.generation != status.attempted_generation);
  auto* ready = camera.capture(status, length);
  assert(ready && length == jpeg.size());
  esp_camera_fb_return(ready);
  camera.describe(status);
  assert(status.generation == status.attempted_generation && !status.config_error);
}
int main() {
  sensor.id.PID = OV2640_PID; sensor.set_framesize = size; sensor.set_quality = quality;
  sensor.set_brightness = brightness_fail;
  CameraControl camera;
  Header status;
  Config config;
  camera.configure(config);
  assert(camera.can_capture() && starts == 1);
  warmup(camera, status);
  assert(returns == 4);
  camera.configure(config); assert(starts == 1);
  capture_ok = false;
  size_t length = 0;
  assert(!camera.capture(status, length) && status.capture_errors == 1);
  config.generation++; config.settings[0] = 1;
  camera.configure(config); camera.describe(status);
  assert(!camera.can_capture() && status.config_error == 32);
  assert(!camera.capture(status, length));
  config = Config{}; config.generation = 3;
  capture_ok = true;
  camera.configure(config); warmup(camera, status);
  assert(status.generation == 3 && starts == 3);
  init_ok = false;
  config.generation++;
  camera.configure(config); camera.describe(status);
  assert(!camera.can_capture() && status.config_error == kConfigInit);
  init_ok = true;
  camera.configure(config); assert(starts == 4); // No unbounded retry of a rejected request.
  config.generation++; config.width = 800; config.height = 600; config.xclk_mhz = 10;
  camera.configure(config); warmup(camera, status);
  assert(status.width == 800 && status.height == 600 && status.sensor_pid == 0x26);
  config.generation++;
  camera.configure(config);
  for (unsigned i = 0; i < 3; ++i) assert(!camera.capture(status, length));
  jpeg[10] ^= 1; // Wrong SOF width must never confirm this configuration.
  for (unsigned i = 0; i < 8; ++i) assert(!camera.capture(status, length) && !held);
  camera.describe(status);
  assert(status.config_error == kConfigFrame && !camera.can_capture());
  assert(status.generation != config.generation);
  config.generation++; config.max_jpeg = 1024;
  camera.configure(config);
  for (unsigned i = 0; i < 3; ++i) assert(!camera.capture(status, length));
  jpeg.insert(jpeg.end() - 2, 2000, 0x42);
  frame.buf = jpeg.data(); frame.len = jpeg.size();
  quality_ok = false; // Oversize adaptation fails in the actual controller.
  assert(!camera.capture(status, length) && !held && status.oversized == 1);
  camera.describe(status);
  assert(status.config_error == kConfigQuality && !camera.can_capture());
  quality_ok = true; config.generation++;
  camera.configure(config); warmup(camera, status);
  assert(!status.config_error && status.generation == config.generation);
  assert(!held);
  puts("Camera lifecycle passed: real controller, warmup, buffer returns, failures and recovery.");
}
