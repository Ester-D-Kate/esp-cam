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
    {1280,1024,FRAMESIZE_SXGA},{1600,1200,FRAMESIZE_UXGA}};
  for (const auto& size : sizes) if (size.width == w && size.height == h) return size.value;
  return FRAMESIZE_INVALID;
}
inline int set_gain_ceiling(sensor_t* s, int value) {
  return s->set_gainceiling(s, gainceiling_t(value));
}
inline uint32_t apply_sensor_config(sensor_t* sensor, const Config& c) {
  if (!valid_config(c)) return kConfigInvalid;
  if (!sensor || (sensor->id.PID != OV2640_PID && sensor->id.PID != OV3660_PID)) return kConfigSensor;
  if (!sensor->set_framesize || !sensor->set_quality) return kConfigSensor;
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
  if (size == FRAMESIZE_INVALID || sensor->set_framesize(sensor, size)) return kConfigGeometry;
  if (sensor->set_quality(sensor, c.quality)) return kConfigQuality;
  for (unsigned i = 0; i < kSettingCount; ++i)
    if (c.settings[i] != kDefaultSetting && setters[i](sensor, c.settings[i])) return setting_error(Setting(i));
  return 0;
}
} // namespace usb_cam
