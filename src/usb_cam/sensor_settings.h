#pragma once
#include <sensor.h>
#include "usb_cam/configuration.h"

namespace usb_cam {
inline uint32_t setting_error(Setting setting) { return 32 + unsigned(setting); }
inline framesize_t framesize_for(uint16_t w, uint16_t h) {
  struct Size { uint16_t width, height; framesize_t value; };
  static const Size sizes[] = {
    {96,96,FRAMESIZE_96X96},{160,120,FRAMESIZE_QQVGA},{176,144,FRAMESIZE_QCIF},
    {240,176,FRAMESIZE_HQVGA},{240,240,FRAMESIZE_240X240},{320,240,FRAMESIZE_QVGA},
    {400,296,FRAMESIZE_CIF},{480,320,FRAMESIZE_HVGA},{640,480,FRAMESIZE_VGA},
    {800,600,FRAMESIZE_SVGA},{1024,768,FRAMESIZE_XGA},{1280,720,FRAMESIZE_HD},
    {1280,1024,FRAMESIZE_SXGA},{1600,1200,FRAMESIZE_UXGA},
    {1920,1080,FRAMESIZE_FHD},{2048,1536,FRAMESIZE_QXGA}};
  for (const auto& size : sizes) if (size.width == w && size.height == h) return size.value;
  return FRAMESIZE_INVALID;
}
inline int set_gain_ceiling(sensor_t* s, int value) {
  return s->set_gainceiling(s, gainceiling_t(value));
}
struct SensorUpdate {
  bool geometry = true, quality = true, controls = true;
};
// The pinned OV3660 driver's image-options callback can hide a failed write.
// Verify geometry and orientation after it, then verify each bounded override.
inline bool sensor_register_is(sensor_t* s, int reg, int mask, int value) {
  return s->get_reg(s, reg, mask) == value;
}
inline bool sensor_write_checked(sensor_t* s, int reg, int mask, int value) {
  return !s->set_reg(s, reg, mask, value) && sensor_register_is(s, reg, mask, value);
}
inline bool sensor_word_is(sensor_t* s, int reg, unsigned value) {
  return sensor_register_is(s, reg, 0xff, value >> 8) &&
      sensor_register_is(s, reg + 1, 0xff, value & 255);
}
inline bool sensor_word_checked(sensor_t* s, int reg, unsigned value) {
  return sensor_write_checked(s, reg, 0xff, value >> 8) &&
      sensor_write_checked(s, reg + 1, 0xff, value & 255);
}
inline uint32_t apply_readout(sensor_t* s, const Config& c) {
  const auto& r = c.readout;
  if (!r.enabled) return 0;
  if (s->set_res_raw(s,r.start_x,r.start_y,r.end_x,r.end_y,r.offset_x,r.offset_y,
      r.total_x,r.total_y,c.width,c.height,r.scale,r.binning)) return kConfigTiming;
  const unsigned values[] = {r.start_x,r.start_y,r.end_x,r.end_y,c.width,c.height,
                            r.total_x,r.total_y,r.offset_x,r.offset_y};
  for (unsigned i=0; i<sizeof(values)/sizeof(*values); ++i)
    if (!sensor_word_is(s,0x3800+2*i,values[i])) return kConfigTiming;
  const bool flip=s->status.vflip, mirror=s->status.hmirror;
  const unsigned reg20=(r.binning ? 1 : 0x40) | (flip ? 6 : 0);
  const unsigned reg21=0x20 | (r.binning ? 1 : 0) | (mirror ? 6 : 0);
  const unsigned reg4514=r.binning ? (flip==mirror ? 0xaa : 0xbb) : (mirror ? 0xbb : 0x88);
  if (!sensor_register_is(s,0x3820,0xff,reg20) || !sensor_register_is(s,0x3821,0xff,reg21) ||
      !sensor_register_is(s,0x4514,0xff,reg4514) ||
      !sensor_register_is(s,0x4520,0xff,r.binning ? 0x0b : 0xb0) ||
      !sensor_register_is(s,0x5001,0x20,r.scale ? 0x20 : 0)) return kConfigTiming;
  const unsigned max_exposure=r.total_y-4;
  const unsigned step50=sysclk_hz(c)/r.total_x/100, step60=sysclk_hz(c)/r.total_x/120;
  const unsigned bands50=max_exposure/step50, bands60=max_exposure/step60;
  // Short exposures cannot fit a complete mains-light band. Disable banding
  // in that case, and prohibit night mode/frame insertion from stretching VTS.
  const bool banding=bands50 && bands60;
  if (!sensor_write_checked(s,0x3814,0xff,r.x_increment) ||
      !sensor_write_checked(s,0x3815,0xff,r.y_increment) ||
      !sensor_word_checked(s,0x3a02,max_exposure) || !sensor_word_checked(s,0x3a14,max_exposure) ||
      !sensor_word_checked(s,0x3a08,step50) || !sensor_word_checked(s,0x3a0a,step60) ||
      !sensor_write_checked(s,0x3a0d,0x3f,bands60 > 63 ? 63 : bands60) ||
      !sensor_write_checked(s,0x3a0e,0x3f,bands50 > 63 ? 63 : bands50) ||
      !sensor_write_checked(s,0x3a00,0x34,banding ? 0x30 : 0x10) ||
      !sensor_write_checked(s,0x3a05,0x40,0)) return kConfigTiming;
  const int requested=c.settings[unsigned(Setting::AecValue)];
  if (requested!=kDefaultSetting) {
    // The SDK setter clamps against the current VTS: only call it after the
    // requested raw timing is installed. Readback verifies manual exposure;
    // automatic AEC is allowed to update its own live integration register.
    if (s->set_aec_value(s,requested)) return setting_error(Setting::AecValue);
    if (c.settings[unsigned(Setting::Aec)]==0 &&
        !sensor_register_is(s,0x3500,0xfffff,unsigned(requested)<<4)) return kConfigTiming;
  }
  const int exposure=s->get_reg(s,0x3500,0xfffff);
  if (exposure < 0 || (unsigned(exposure) > (max_exposure<<4) &&
      !sensor_write_checked(s,0x3500,0xfffff,max_exposure<<4))) return kConfigTiming;
  return 0;
}
inline uint32_t validate_sensor_target(sensor_t* sensor, const Config& c) {
  if (!valid_config(c)) return kConfigInvalid;
  if (!sensor || (sensor->id.PID != OV2640_PID && sensor->id.PID != OV3660_PID)) return kConfigSensor;
  if (sensor->id.PID == OV2640_PID && (c.width > 1600 || c.height > 1200)) return kConfigGeometry;
  if (!sensor->set_framesize || !sensor->set_quality) return kConfigSensor;
  if (c.pll.multiplier && (sensor->id.PID != OV3660_PID || !sensor->set_pll)) return kConfigTiming;
  if (c.readout.enabled && (sensor->id.PID != OV3660_PID || !sensor->set_res_raw ||
      !sensor->set_reg || !sensor->get_reg)) return kConfigTiming;
  return 0;
}
inline uint32_t apply_sensor_config(sensor_t* sensor, const Config& c,
                                    SensorUpdate update = {}) {
  const uint32_t validation = validate_sensor_target(sensor, c);
  if (validation) return validation;
  using Setter = int (*)(sensor_t*, int);
  const Setter setters[] = {
    sensor->set_brightness, sensor->set_contrast, sensor->set_saturation,
    sensor->set_sharpness, sensor->set_denoise, sensor->set_hmirror, sensor->set_vflip,
    sensor->set_exposure_ctrl, sensor->set_aec2, sensor->set_ae_level, sensor->set_aec_value,
    sensor->set_gain_ctrl, sensor->set_agc_gain, sensor->set_gainceiling ? set_gain_ceiling : nullptr,
    sensor->set_whitebal, sensor->set_awb_gain, sensor->set_wb_mode, sensor->set_special_effect,
    sensor->set_bpc, sensor->set_wpc, sensor->set_raw_gma, sensor->set_lenc,
    sensor->set_dcw, sensor->set_colorbar};
  static_assert(sizeof(setters) / sizeof(*setters) == kSettingCount, "Setting mapping missing");
  for (unsigned i = 0; i < kSettingCount; ++i)
    if (c.settings[i] != kDefaultSetting && !setters[i]) return setting_error(Setting(i));
  const framesize_t size = framesize_for(c.width, c.height);
  if (update.geometry && (size == FRAMESIZE_INVALID || sensor->set_framesize(sensor, size)))
    return kConfigGeometry;
  // set_framesize rewrites PLL registers. Apply overrides last; re-running
  // geometry when disabling them restores the SDK's sensor-specific timing.
  if (update.geometry && c.pll.multiplier && sensor->set_pll(sensor, 0,
      c.pll.multiplier, c.pll.sys_div, c.pll.root_2x, c.pll.pre_div,
      c.pll.seld5, 1, c.pll.pclk_div)) return kConfigTiming;
  if (update.quality && sensor->set_quality(sensor, c.quality)) return kConfigQuality;
  if (update.controls) {
    for (unsigned i = 0; i < kSettingCount; ++i)
      if (c.settings[i] != kDefaultSetting && !(c.readout.enabled && i==unsigned(Setting::AecValue)) &&
          setters[i](sensor, c.settings[i])) return setting_error(Setting(i));
  }
  // Orientation controls rewrite the sampling increments, so raw readout is last.
  return update.geometry || update.controls ? apply_readout(sensor,c) : 0;
}
inline uint32_t reset_sensor_defaults(sensor_t* sensor) {
  if (!sensor || !sensor->reset || !sensor->init_status || !sensor->set_pixformat)
    return kConfigReset;
  if (sensor->id.PID == OV2640_PID) {
    // Pinned OV2640 set_bank caches its choice before checking the bus write.
    // A failed select can make reset write COM7 in the wrong physical bank.
    // set_reg's full-byte write forces BANK_SEL even when that cache is stale;
    // its encoded high byte also leaves the cache on the sensor bank.
    constexpr int sensor_bank_select = 0x1ff;
    if (!sensor->set_reg || sensor->set_reg(sensor, sensor_bank_select, 0xff, 1))
      return kConfigReset;
  }
  if (sensor->reset(sensor)) return kConfigReset;
  // Match the pinned SDK's OV2640 startup overlay, without freeing I2S,
  // SCCB or the framebuffers. Defaults cannot safely be replayed from status:
  // some cached fields don't have working setters on this sensor.
  if (sensor->id.PID == OV2640_PID) {
    if (!sensor->set_gainceiling || !sensor->set_bpc || !sensor->set_wpc || !sensor->set_lenc ||
        sensor->set_gainceiling(sensor, GAINCEILING_2X) || sensor->set_bpc(sensor, 0) ||
        sensor->set_wpc(sensor, 1) || sensor->set_lenc(sensor, 1)) return kConfigReset;
  }
  // Refresh cached orientation before geometry uses it; never refresh status
  // after explicit controls, because init_status hardcodes some defaults.
  if (sensor->init_status(sensor) || sensor->set_pixformat(sensor, PIXFORMAT_JPEG)) return kConfigReset;
  return 0;
}
} // namespace usb_cam
