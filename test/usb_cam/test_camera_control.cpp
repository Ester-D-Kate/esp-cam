#include <cassert>
#include <cstdio>
#include <vector>
#include <Arduino.h>
#include "camera_control.h"
#include "sensor_settings.h"

using namespace usb_cam;
TestSerial Serial;
static bool psram = true, init_ok = true, capture_ok = true, held = false;
static bool quality_ok = true, reset_ok = true, clock_ok = true, brightness_ok = true;
static bool bank_select_ok = true;
static int hardware_bank = 1, cached_bank = 1;
static unsigned starts = 0, stops = 0, returns = 0, resets = 0, writes = 0, clocks = 0;
static int actual_brightness = 0, actual_clock = 20;
static int actual_pll = 0;
static bool pll_ok = true;
static int pll(sensor_t*, int, int multiplier, int, int, int, int, int, int) {
  actual_pll = multiplier;
  return pll_ok ? 0 : -1;
}
static sensor_t sensor;
static camera_fb_t frame;
static std::vector<uint8_t> jpeg;
bool psramFound() { return psram; }
uint32_t ledc_get_freq(ledc_mode_t mode, ledc_timer_t timer) {
  assert(mode == LEDC_LOW_SPEED_MODE && timer == LEDC_TIMER_0);
  return actual_clock * 1000000u;
}
static int quality(sensor_t*, int) { ++writes; return quality_ok ? 0 : -1; }
static int brightness(sensor_t*, int value) {
  ++writes; actual_brightness = value; // A failed I/O can leave partial changes.
  return brightness_ok ? 0 : -1;
}
static int reset(sensor_t*) {
  assert(!held); ++resets;
  if (!reset_ok) return -1;
  // Model the pinned OV2640 cache: a failed bank-select can leave the cache at
  // SENSOR while hardware is still DSP. reset then skips the physical select.
  if (cached_bank != 1) { cached_bank = 1; hardware_bank = 1; }
  if (hardware_bank == 1) actual_brightness = 0;
  return 0;
}
static int set_reg(sensor_t*, int reg, int mask, int value) {
  assert(reg == 0x1ff && mask == 0xff && value == 1);
  cached_bank = 1;
  if (!bank_select_ok) return -1;
  hardware_bank = 1;
  return 0;
}
static int init_status(sensor_t* s) { s->status.brightness = 0; return 0; }
static int pixformat(sensor_t*, pixformat_t value) { return value == PIXFORMAT_JPEG ? 0 : -1; }
static int baseline(sensor_t*, int) { return 0; }
static int gainceiling(sensor_t*, gainceiling_t value) { return value == GAINCEILING_2X ? 0 : -1; }
static int xclk(sensor_t* s, int timer, int mhz) {
  assert(timer == LEDC_TIMER_0 && !held); ++clocks;
  s->xclk_freq_hz = mhz * 1000000; // Real callback changes cache even on failure.
  if (!clock_ok) return -1;
  actual_clock = mhz; return 0;
}
static int size(sensor_t*, framesize_t value) {
  ++writes; actual_pll = 0;
  if (value != FRAMESIZE_VGA && value != FRAMESIZE_SVGA && value != FRAMESIZE_UXGA) return -1;
  const unsigned w = value == FRAMESIZE_UXGA ? 1600 : value == FRAMESIZE_VGA ? 640 : 800;
  const unsigned h = value == FRAMESIZE_UXGA ? 1200 : value == FRAMESIZE_VGA ? 480 : 600;
  jpeg = {0xff,0xd8,0xff,0xc0,0,11,8,uint8_t(h>>8),uint8_t(h),uint8_t(w>>8),uint8_t(w),
          1,1,0x11,0,0xff,0xda,0,8,1,1,0,0,63,0,0x42,0xff,0xd9};
  frame = {}; frame.buf = jpeg.data(); frame.len = jpeg.size(); frame.width = w; frame.height = h;
  frame.format = PIXFORMAT_JPEG;
  return 0;
}
extern "C" esp_err_t esp_camera_init(const camera_config_t* config) {
  assert(!held && config->frame_size == FRAMESIZE_QXGA && config->fb_count == 2);
  assert(config->pin_d0 == 5 && config->pin_xclk == 0 && config->pin_sccb_sda == 26);
  ++starts; actual_clock = config->xclk_freq_hz / 1000000;
  return init_ok ? ESP_OK : ESP_FAIL;
}
extern "C" esp_err_t esp_camera_deinit() { assert(!held); ++stops; return ESP_OK; }
extern "C" sensor_t* esp_camera_sensor_get() { return &sensor; }
extern "C" camera_fb_t* esp_camera_fb_get() {
  assert(!held);
  if (!capture_ok) return nullptr;
  held = true; return &frame;
}
extern "C" void esp_camera_fb_return(camera_fb_t* returned) {
  assert(held && returned == &frame); held = false; ++returns;
}
static void confirm(CameraControl& camera, Header& status, unsigned discard = 3) {
  size_t length = 0;
  for (unsigned i = 0; i < discard; ++i) assert(!camera.capture(status, length) && !held);
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
  sensor.set_brightness = brightness; sensor.reset = reset; sensor.init_status = init_status;
  sensor.set_reg = set_reg;
  sensor.set_pixformat = pixformat; sensor.set_xclk = xclk;
  sensor.set_gainceiling = gainceiling; sensor.set_bpc = baseline;
  sensor.set_wpc = baseline; sensor.set_lenc = baseline;
  CameraControl camera;
  Header status;
  Config config;
  camera.configure(config);
  assert(camera.can_capture() && starts == 1);
  confirm(camera, status);
  assert(returns == 4);
  assert(status.camera_starts == 1 && status.sensor_resets == 0 && status.xclk_hz == 20000000);
  camera.configure(config); assert(starts == 1);

  // Regression: a repeated setting or FPS-only change must not tear down I2S,
  // SCCB or buffers, nor write unchanged sensor settings.
  const unsigned first_writes = writes;
  init_ok = false;
  config.generation++; config.interval_100ns = 400000;
  camera.configure(config);
  assert(starts == 1 && stops == 0 && writes == first_writes && resets == 0);
  confirm(camera, status, 0);
  config.generation++; config.width = 800; config.height = 600;
  camera.configure(config); confirm(camera, status);
  assert(starts == 1 && stops == 0 && resets == 0 && status.width == 800);
  config.generation++; config.quality = 20;
  camera.configure(config); confirm(camera, status);
  assert(starts == 1 && resets == 0 && status.quality == 20);

  // Explicit -> default and partial setter failure restore real sensor defaults
  // through a sensor reset, without restarting the camera peripheral.
  config.generation++; config.settings[0] = 1;
  camera.configure(config); confirm(camera, status);
  assert(actual_brightness == 1 && resets == 1);
  config.generation++; config.settings[0] = kDefaultSetting;
  camera.configure(config); confirm(camera, status);
  assert(actual_brightness == 0 && resets == 2);
  brightness_ok = false;
  config.generation++; config.settings[0] = 2;
  camera.configure(config); camera.describe(status);
  assert(!camera.can_capture() && status.config_error == 32 && actual_brightness == 2);
  const unsigned failed_resets = resets;
  camera.configure(config); assert(resets == failed_resets); // No unbounded retries.
  hardware_bank = 0; cached_bank = 1; // Simulate the SDK's stale bank after failed I/O.
  brightness_ok = true; config.generation++; config.settings[0] = kDefaultSetting;
  camera.configure(config); confirm(camera, status);
  assert(actual_brightness == 0 && resets == failed_resets + 1);

  // A four-second fb_get timeout must stop the capture loop and expose failure.
  capture_ok = false;
  size_t length = 0;
  assert(!camera.capture(status, length) && status.capture_errors == 1);
  camera.describe(status);
  assert(!camera.can_capture() && status.config_error == kConfigFrame);
  assert(!camera.capture(status, length) && status.capture_errors == 1);
  capture_ok = true; config.generation++;
  camera.configure(config); confirm(camera, status);
  assert(starts == 1 && stops == 0);

  // Sensor reset failures remain visible and a changed request can recover.
  reset_ok = false; config.generation++; config.settings[0] = 1;
  camera.configure(config); camera.describe(status);
  assert(!camera.can_capture() && status.config_error == 7);
  reset_ok = true; config.generation++;
  camera.configure(config); confirm(camera, status);
  const unsigned before_bank_failure = resets;
  bank_select_ok = false; config.generation++; config.settings[0] = kDefaultSetting;
  camera.configure(config); camera.describe(status);
  assert(!camera.can_capture() && status.config_error == 7 && resets == before_bank_failure);
  bank_select_ok = true; config.generation++;
  camera.configure(config); confirm(camera, status);

  // Clock callbacks can poison their own cache on failure. Returning to the old
  // requested clock must still repair hardware; don't trust that cache.
  config.generation++; config.xclk_mhz = 10;
  camera.configure(config); confirm(camera, status);
  assert(actual_clock == 10 && starts == 1);
  assert(status.xclk_hz == 10000000);
  clock_ok = false; config.generation++; config.xclk_mhz = 16;
  camera.configure(config); camera.describe(status);
  assert(!camera.can_capture() && status.config_error == 8);
  assert(status.xclk_hz == 0);
  const unsigned failed_clocks = clocks;
  clock_ok = true; config.generation++; config.xclk_mhz = 10;
  camera.configure(config); confirm(camera, status);
  assert(clocks == failed_clocks + 1 && actual_clock == 10);

  // Corrupt geometry must never be acknowledged as a successful request.
  config.generation++;
  camera.configure(config);
  jpeg[10] ^= 1;
  for (unsigned i = 0; i < 8; ++i) assert(!camera.capture(status, length) && !held);
  camera.describe(status);
  assert(status.config_error == kConfigFrame && !camera.can_capture());
  assert(status.generation != config.generation);
  config.generation++; config.max_jpeg = 1024;
  camera.configure(config);
  for (unsigned i = 0; i < 3; ++i) assert(!camera.capture(status, length));
  jpeg.insert(jpeg.end() - 2, 2000, 0x42);
  frame.buf = jpeg.data(); frame.len = jpeg.size();
  quality_ok = false;
  assert(!camera.capture(status, length) && !held && status.oversized == 1);
  camera.describe(status);
  assert(status.config_error == kConfigQuality && !camera.can_capture());
  quality_ok = true; config.generation++;
  camera.configure(config); confirm(camera, status);
  assert(starts == 1 && stops == 0 && !held);
  assert(status.camera_starts == 1 && status.sensor_resets == resets + 1);

  // Startup failures still retry initialization only on a new request.
  CameraControl startup;
  startup.configure(config); startup.describe(status);
  assert(!startup.can_capture() && status.config_error == kConfigInit && starts == 2);
  startup.configure(config); assert(starts == 2);
  init_ok = true; config.generation++;
  startup.configure(config); confirm(startup, status);
  assert(starts == 3 && !held);

  // Regression: the UXGA/15 preset must not reject every real frame merely
  // because it exceeds the old 48 KiB rate budget but fits our 64 KiB slots.
  config = mode_config(9, USB_CAM_INTERVAL(15));
  config.generation = 100;
  startup.configure(config); confirm(startup, status);
  const unsigned oversized_before = status.oversized;
  for (const size_t bytes : {size_t(52 * 1024), size_t(64 * 1024)}) {
    jpeg.insert(jpeg.end() - 2, bytes - jpeg.size(), 0x42);
    frame.buf = jpeg.data(); frame.len = jpeg.size();
    auto* ready = startup.capture(status, length);
    assert(ready && length == bytes && status.oversized == oversized_before);
    esp_camera_fb_return(ready);
    assert(!held);
  }
  jpeg.insert(jpeg.end() - 2, 1, 0x42);
  frame.buf = jpeg.data(); frame.len = jpeg.size();
  assert(!startup.capture(status, length) && !held);
  assert(status.oversized == oversized_before + 1);
  // Reject sensor-incompatible requests before a live clock change.
  config = Config{}; config.generation = 200; config.xclk_mhz = 24;
  config.width = 2048; config.height = 1536;
  const unsigned clocks_before = clocks;
  startup.configure(config); startup.describe(status);
  assert(!startup.can_capture() && status.config_error == kConfigGeometry && clocks == clocks_before);
  // PLL changes and disabling the override must both re-run geometry, with no
  // camera driver restart; a failed PLL callback must not confirm frames.
  sensor.id.PID = OV3660_PID; sensor.set_pll = pll;
  CameraControl ov3660;
  config = Config{}; config.generation = 300; config.pll.multiplier = 30;
  ov3660.configure(config); confirm(ov3660, status);
  assert(actual_pll == 30 && status.capabilities == kSensorProfilesCapability);
  const unsigned starts_before = starts;
  config.generation++; config.interval_100ns = USB_CAM_INTERVAL(25);
  ov3660.configure(config); confirm(ov3660, status, 0);
  assert(actual_pll == 30);
  config.generation++; config.pll.multiplier = 0;
  ov3660.configure(config); confirm(ov3660, status);
  assert(actual_pll == 0 && starts == starts_before);
  pll_ok = false; config.generation++; config.pll.multiplier = 30;
  ov3660.configure(config); ov3660.describe(status);
  assert(!ov3660.can_capture() && status.config_error == kConfigTiming);
  assert(status.generation != config.generation);
  pll_ok = true; config.generation++; config.pll.multiplier = 0;
  ov3660.configure(config); confirm(ov3660, status);
  assert(actual_pll == 0 && starts == starts_before);
  puts("Camera lifecycle passed: persistent driver, defaults, frame timeout, clock and recovery.");
}
