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
inline uint32_t validate_sensor_target(sensor_t* sensor, const Config& c) {
  if (!valid_config(c)) return kConfigInvalid;
  if (!sensor || (sensor->id.PID != OV2640_PID && sensor->id.PID != OV3660_PID)) return kConfigSensor;
  if (sensor->id.PID == OV2640_PID && (c.width > 1600 || c.height > 1200)) return kConfigGeometry;
  if (!sensor->set_framesize || !sensor->set_quality) return kConfigSensor;
  if (c.pll.multiplier && (sensor->id.PID != OV3660_PID || !sensor->set_pll)) return kConfigTiming;
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
      if (c.settings[i] != kDefaultSetting && setters[i](sensor, c.settings[i])) return setting_error(Setting(i));
  }
  return 0;
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
