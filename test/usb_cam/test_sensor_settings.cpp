#include <cassert>
#include <cstdio>
#include "sensor_settings.h"
#include "usb_cam/config_session.h"
using namespace usb_cam;
static unsigned writes, pll_writes;
static bool pll_ok = true;
static int set_pll(sensor_t*, int bypass, int mul, int sys, int root, int pre, int seld, int manual, int div) {
  assert(writes == 1); // Geometry first, then timing, before JPEG quality.
  assert(!bypass && mul == 30 && sys == 1 && !root && pre == 3 && !seld && manual && div == 10);
  ++pll_writes; return pll_ok ? 0 : -1;
}
static int set_size(sensor_t*, framesize_t size) { ++writes; return size == FRAMESIZE_SVGA ? 0 : -1; }
static int set_quality(sensor_t*, int quality) { ++writes; return quality == 12 ? 0 : -1; }
static int fail_setting(sensor_t*, int) { ++writes; return -1; }
int main() {
  Config config = mode_config(2, USB_CAM_INTERVAL(30));
  sensor_t sensor{};
  sensor.id.PID = OV2640_PID;
  sensor.set_framesize = set_size; sensor.set_quality = set_quality;
  assert(apply_sensor_config(&sensor, config) == 0 && writes == 2);
  config.settings[unsigned(Setting::Brightness)] = 1;
  writes = 0;
  assert(apply_sensor_config(&sensor, config) == setting_error(Setting::Brightness));
  assert(writes == 0); // Unsupported control rejected before changing geometry.
  sensor.set_brightness = fail_setting;
  assert(apply_sensor_config(&sensor, config) == setting_error(Setting::Brightness));
  config.settings[unsigned(Setting::Brightness)] = 5;
  writes = 0;
  assert(apply_sensor_config(&sensor, config) == kConfigInvalid && writes == 0);
  config = mode_config(2, USB_CAM_INTERVAL(30));
  config.width = 1920; config.height = 1080;
  writes = 0;
  assert(apply_sensor_config(&sensor, config) == kConfigGeometry && writes == 0);
  config = mode_config(2, USB_CAM_INTERVAL(30));
  config.pll.multiplier = 30;
  writes = 0;
  assert(apply_sensor_config(&sensor, config) == kConfigTiming && writes == 0);
  sensor.id.PID = OV3660_PID;
  assert(apply_sensor_config(&sensor, config) == kConfigTiming && writes == 0);
  sensor.set_pll = set_pll;
  assert(apply_sensor_config(&sensor, config) == 0 && writes == 2 && pll_writes == 1);
  pll_ok = false; writes = 0;
  assert(apply_sensor_config(&sensor, config) == kConfigTiming && writes == 1);
  sensor.id.PID = OV2640_PID;
  ConfigSession session;
  config = mode_config(2, USB_CAM_INTERVAL(30));
  assert(session.request(config));
  assert(!session.active());
  assert(!session.confirm(640, 480));
  assert(session.confirm(800, 600));
  assert(session.active());
  assert(!session.request(config)); // Repeated desired state doesn't restart camera.
  config.generation++;
  assert(session.request(config));
  session.fail(5);
  assert(!session.active() && !session.can_capture() && session.error() == 5);
  assert(!session.request(config)); // Failed requests don't retry forever.
  config.quality++; // Changed contents, same generation after another controller reboot.
  assert(session.request(config) && session.can_capture());
  assert(session.confirm(800, 600));
  assert(session.applied_generation() == 2);
  config.width = 1920;
  assert(!session.request(config) && session.active());
  puts("Sensor tests passed: prevalidation, callback failure, confirmation and recovery.");
}
