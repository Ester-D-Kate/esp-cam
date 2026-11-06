#include <cassert>
#include <cstdio>
#include <map>
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
static std::map<int, int> registers;
static int fail_register = -1;
static bool conceal_failure;
static int raw_write(sensor_t*, int reg, int mask, int value) {
  if (reg == fail_register) return -1;
  registers[reg] = (registers[reg] & ~mask) | (value & mask); return 0;
}
static int raw_read(sensor_t*, int reg, int mask) { return registers[reg] & mask; }
static void word(int reg, int value) { registers[reg] = value >> 8; registers[reg+1] = value & 255; }
static int raw_size(sensor_t*, framesize_t) { ++writes; word(0x380e,783); return 0; }
static bool exposure_corrupt;
static int raw_aec(sensor_t*,int value) {
  const int maximum=(registers[0x380e]<<8)|registers[0x380f];
  registers[0x3500]=(exposure_corrupt ? 1 : value>maximum ? maximum : value)<<4;
  return 0;
}
static int raw_auto_exposure(sensor_t*,int) { return 0; }
static int raw_quality(sensor_t*, int) { ++writes; return 0; }
static int raw_pll(sensor_t*, int, int, int, int, int, int, int, int) { ++writes; return 0; }
static int raw_resolution(sensor_t* s, int sx, int sy, int ex, int ey, int ox, int oy,
    int tx, int ty, int w, int h, bool scale, bool binning) {
  assert(writes == 3); // Framesize, PLL, quality precede raw geometry.
  word(0x3800,sx); word(0x3802,sy); word(0x3804,ex); word(0x3806,ey);
  word(0x3808,w); word(0x380a,h); word(0x380c,tx); word(0x380e,ty);
  word(0x3810,ox); word(0x3812,oy);
  registers[0x5001] = scale ? 0x20 : 0;
  registers[0x3820] = binning ? 1 : 0x40;
  registers[0x3821] = 0x20 | (binning ? 1 : 0);
  registers[0x4514] = binning ? 0xaa : 0x88;
  registers[0x4520] = binning ? 0x0b : 0xb0;
  if (conceal_failure) registers[0x3821] = 0; // SDK can incorrectly return success.
  s->status.binning = binning;
  return 0;
}
static void test_raw_readout() {
  Config c = profile_defaults(USB_CAM_PROFILE_OV3660);
  auto& r = c.readout;
  r.enabled = r.binning = true; r.x_increment = r.y_increment = 0x31;
  r.start_x=384; r.start_y=288; r.end_x=1695; r.end_y=1259;
  r.offset_x=8; r.offset_y=3; r.total_x=1600; r.total_y=556;
  sensor_t s{}; s.id.PID=OV3660_PID;
  s.set_framesize=raw_size; s.set_quality=raw_quality; s.set_pll=raw_pll;
  writes=0;
  assert(apply_sensor_config(&s,c)==kConfigTiming && writes==0);
  s.set_res_raw=raw_resolution; s.set_reg=raw_write; s.get_reg=raw_read;
  assert(apply_sensor_config(&s,c)==0);
  assert(registers[0x3814]==0x31 && registers[0x3815]==0x31);
  assert((registers[0x3a02]<<8 | registers[0x3a03])==552);
  assert((registers[0x3a14]<<8 | registers[0x3a15])==552);
  assert((registers[0x3a08]<<8 | registers[0x3a09])==333);
  assert((registers[0x3a0a]<<8 | registers[0x3a0b])==277);
  assert(registers[0x3a0d]==1 && registers[0x3a0e]==1);
  assert((registers[0x3a00]&0x34)==0x30 && !(registers[0x3a05]&0x40));
  writes=0; conceal_failure=true;
  assert(apply_sensor_config(&s,c)==kConfigTiming);
  conceal_failure=false; writes=0; fail_register=0x3a14;
  assert(apply_sensor_config(&s,c)==kConfigTiming);
  fail_register=-1; writes=0;
  c.width=320; c.height=240; c.interval_100ns=USB_CAM_INTERVAL(120);
  r.x_increment=r.y_increment=0x71; r.start_y=290; r.end_y=1257;
  r.offset_x=4; r.offset_y=1; r.total_y=278;
  assert(valid_config(c) && apply_sensor_config(&s,c)==0);
  assert(registers[0x3814]==0x71 && (registers[0x3a00]&0x34)==0x10);
  writes=0; c.settings[unsigned(Setting::AecValue)]=275;
  assert(apply_sensor_config(&s,c)==kConfigInvalid && writes==0);
  c.settings[unsigned(Setting::AecValue)]=kDefaultSetting;
  c.settings[unsigned(Setting::Aec2)]=1;
  assert(apply_sensor_config(&s,c)==kConfigInvalid && writes==0);
  c.settings[unsigned(Setting::Aec2)]=kDefaultSetting; s.id.PID=OV2640_PID;
  assert(apply_sensor_config(&s,c)==kConfigTiming && writes==0);
  s.id.PID=OV3660_PID; s.set_aec_value=raw_aec; s.set_exposure_ctrl=raw_auto_exposure;
  c=mode_config(1,USB_CAM_INTERVAL(60),USB_CAM_PROFILE_OV3660_FULL);
  c.readout.total_y=1000;
  c.settings[unsigned(Setting::Aec)]=0; c.settings[unsigned(Setting::AecValue)]=900;
  writes=0;
  assert(apply_sensor_config(&s,c)==0 && registers[0x3500]==(900<<4));
  exposure_corrupt=true; writes=0;
  assert(apply_sensor_config(&s,c)==kConfigTiming);
}
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
  test_raw_readout();
  puts("Sensor tests passed: prevalidation, readout verification, exposure, confirmation and recovery.");
}
