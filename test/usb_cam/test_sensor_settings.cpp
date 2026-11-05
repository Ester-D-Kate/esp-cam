#include <cassert>
#include <cstdio>
#include "sensor_settings.h"
#include "usb_cam/config_session.h"
using namespace usb_cam;
static unsigned writes;
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
