#pragma once
#include <cstring>
#include "modes.h"
#include "wire.h"
namespace usb_cam {
constexpr size_t kControlSize = 128;
constexpr uint32_t kMaxJpeg = USB_CAM_BUFFER_BYTES;
constexpr int16_t kDefaultSetting = -32768;
// Sensor configuration status codes shared with the Pico's recovery policy.
enum ConfigError : uint32_t {
  kConfigInvalid = 1, kConfigInit = 2, kConfigSensor = 3,
  kConfigGeometry = 4, kConfigQuality = 5, kConfigFrame = 6,
  kConfigReset = 7, kConfigClock = 8, kConfigTiming = 9
};
// Stable wire IDs; append new entries without renumbering existing controls.
#define USB_CAM_SETTINGS(X) \
 X(Brightness, brightness, -2, 2) X(Contrast, contrast, -2, 2) \
 X(Saturation, saturation, -2, 2) X(Sharpness, sharpness, -2, 2) \
 X(Denoise, denoise, 0, 8) X(Hmirror, hmirror, 0, 1) X(Vflip, vflip, 0, 1) \
 X(Aec, aec, 0, 1) X(Aec2, aec2, 0, 1) X(AeLevel, ae_level, -2, 2) \
 X(AecValue, aec_value, 0, 1200) X(Agc, agc, 0, 1) X(AgcGain, agc_gain, 0, 30) \
 X(GainCeiling, gainceiling, 0, 6) X(Awb, awb, 0, 1) X(AwbGain, awb_gain, 0, 1) \
 X(WbMode, wb_mode, 0, 4) X(SpecialEffect, special_effect, 0, 6) \
 X(Bpc, bpc, 0, 1) X(Wpc, wpc, 0, 1) X(RawGamma, raw_gma, 0, 1) \
 X(LensCorrection, lenc, 0, 1) X(Downsize, dcw, 0, 1) X(Colorbar, colorbar, 0, 1)
#define USB_CAM_ENUM(symbol, name, low, high) symbol,
enum class Setting : uint8_t { USB_CAM_SETTINGS(USB_CAM_ENUM) Count };
#undef USB_CAM_ENUM
constexpr unsigned kSettingCount = unsigned(Setting::Count);
struct SettingSpec { const char* name; int16_t minimum, maximum; };
#define USB_CAM_SPEC(symbol, name, low, high) {#name, low, high},
constexpr SettingSpec kSettingSpecs[] = {USB_CAM_SETTINGS(USB_CAM_SPEC)};
#undef USB_CAM_SPEC
constexpr uint32_t kSensorProfilesCapability = USB_CAM_CAP_SENSOR_PROFILES;
struct SensorPll {
  uint8_t multiplier = 0; // Zero preserves SDK timing and the legacy wire token.
  uint8_t sys_div = 1, pre_div = 3, root_2x = 0, seld5 = 0, pclk_div = 10;
};
inline bool valid_pll(const SensorPll& p, unsigned xclk_mhz) {
  if (!p.multiplier) return true;
  if (p.multiplier > 31 || !p.sys_div || p.sys_div > 15 || p.pre_div > 3 ||
      p.root_2x > 1 || p.seld5 > 3 || !p.pclk_div || p.pclk_div > 31) return false;
  constexpr unsigned pre2[] = {2, 3, 4, 6}, seld2[] = {2, 2, 4, 5};
  // Rational comparisons avoid rounding an out-of-range clock into acceptance.
  const uint64_t vco_n = uint64_t(xclk_mhz) * 1000000 * p.multiplier * (p.root_2x + 1) * 2;
  const uint64_t vco_d = pre2[p.pre_div];
  const uint64_t pll_d = vco_d * p.sys_div * seld2[p.seld5];
  return vco_n >= 150000000u * vco_d && vco_n <= 500000000u * vco_d &&
      vco_n * 2 <= 54000000u * pll_d * 4 &&
      vco_n * 2 <= 10000000u * pll_d * 2 * p.pclk_div;
}
inline bool pll_equal(const SensorPll& a, const SensorPll& b) {
  return a.multiplier == b.multiplier && (!a.multiplier ||
      (a.sys_div == b.sys_div && a.pre_div == b.pre_div && a.root_2x == b.root_2x &&
       a.seld5 == b.seld5 && a.pclk_div == b.pclk_div));
}
struct Config {
  uint32_t generation = 1;
  uint16_t width = 640, height = 480;
  uint32_t interval_100ns = USB_CAM_INTERVAL(30);
  uint32_t max_jpeg = uint32_t(uint64_t(USB_CAM_BYTE_BUDGET) * USB_CAM_INTERVAL(30) / 10000000);
  uint8_t quality = 16, xclk_mhz = 20;
  SensorPll pll;
  int16_t settings[kSettingCount];
  Config() { for (auto& value : settings) value = kDefaultSetting; }
};
inline bool supported_geometry(uint16_t w, uint16_t h) {
  // SDK geometries through QXGA; the sensor-specific check runs before writes.
  static const uint16_t sizes[][2] = {{96,96},{160,120},{176,144},{240,176},{240,240},
    {320,240},{400,296},{480,320},{640,480},{800,600},{1024,768},{1280,720},
    {1280,1024},{1600,1200},{1920,1080},{2048,1536}};
  for (const auto& size : sizes) if (size[0] == w && size[1] == h) return true;
  return false;
}
inline bool supported_xclk(int mhz) {
  return mhz == 10 || mhz == 16 || mhz == 20 || mhz == 24;
}
inline bool valid_config(const Config& c) {
  if (!supported_geometry(c.width, c.height) || c.interval_100ns < USB_CAM_INTERVAL(60) ||
      c.interval_100ns > 10000000 || c.max_jpeg < 1024 || c.max_jpeg > kMaxJpeg ||
      c.quality > 50 || !supported_xclk(c.xclk_mhz) || !valid_pll(c.pll, c.xclk_mhz)) return false;
  for (unsigned i = 0; i < kSettingCount; ++i) {
    if (c.settings[i] != kDefaultSetting &&
        (c.settings[i] < kSettingSpecs[i].minimum || c.settings[i] > kSettingSpecs[i].maximum)) return false;
  }
  return true;
}
inline void encode_config(const Config& c, uint8_t* out) {
  memset(out, 0, kControlSize);
  put32(out, 0x32474643); // CFG2
  put16(out + 4, 2); put16(out + 6, kControlSize);
  put32(out + 8, c.generation); put16(out + 12, c.width); put16(out + 14, c.height);
  put32(out + 16, c.interval_100ns); put32(out + 20, c.max_jpeg);
  out[24] = c.quality; out[25] = c.xclk_mhz;
  for (unsigned i = 0; i < kSettingCount; ++i) put16(out + 28 + 2 * i, uint16_t(c.settings[i]));
  if (c.pll.multiplier) {
    out[76] = c.pll.multiplier; out[77] = c.pll.sys_div; out[78] = c.pll.pre_div;
    out[79] = c.pll.root_2x; out[80] = c.pll.seld5; out[81] = c.pll.pclk_div;
  }
  put32(out + 124, crc32(out, 124));
}
enum class ConfigDecodeError : uint32_t {
  None = 0, Length = 1, Magic = 2, Version = 3, EncodedLength = 4,
  Checksum = 5, Reserved = 6, Values = 7
};
inline bool decode_config(const uint8_t* data, size_t length, Config& out,
                          ConfigDecodeError* error = nullptr) {
  const auto fail = [error](ConfigDecodeError reason) {
    if (error) *error = reason;
    return false;
  };
  if (error) *error = ConfigDecodeError::None;
  if (!data || length != kControlSize) return fail(ConfigDecodeError::Length);
  if (get32(data) != 0x32474643) return fail(ConfigDecodeError::Magic);
  if (get16(data + 4) != 2) return fail(ConfigDecodeError::Version);
  if (get16(data + 6) != kControlSize) return fail(ConfigDecodeError::EncodedLength);
  if (get32(data + 124) != crc32(data, 124)) return fail(ConfigDecodeError::Checksum);
  if (data[26] || data[27]) return fail(ConfigDecodeError::Reserved);
  static_assert(28 + 2 * kSettingCount == 76, "Review PLL wire offsets when adding settings");
  if (!data[76]) for (size_t i = 77; i < 82; ++i) if (data[i]) return fail(ConfigDecodeError::Reserved);
  for (size_t i = 82; i < 124; ++i) {
    if (data[i]) return fail(ConfigDecodeError::Reserved);
  }
  Config c;
  c.generation = get32(data + 8); c.width = get16(data + 12); c.height = get16(data + 14);
  c.interval_100ns = get32(data + 16); c.max_jpeg = get32(data + 20);
  c.quality = data[24]; c.xclk_mhz = data[25];
  if (data[76]) c.pll = {data[76], data[77], data[78], data[79], data[80], data[81]};
  for (unsigned i = 0; i < kSettingCount; ++i) {
    const uint16_t raw = get16(data + 28 + 2 * i);
    c.settings[i] = raw < 0x8000 ? int16_t(raw) : int16_t(int32_t(raw) - 65536);
  }
  if (!valid_config(c)) return fail(ConfigDecodeError::Values);
  out = c;
  return true;
}
inline uint32_t config_token(const Config& c) {
  uint8_t bytes[kControlSize]; encode_config(c, bytes); return get32(bytes + 124);
}
inline bool config_equal(const Config& a, const Config& b) {
  uint8_t x[kControlSize], y[kControlSize]; encode_config(a, x); encode_config(b, y);
  return memcmp(x, y, sizeof(x)) == 0;
}
inline Config mode_config(unsigned index, uint32_t interval, UsbCamProfile profile = USB_CAM_PROFILE_OV2640) {
  Config c;
  const UsbCamMode* mode = usb_cam_profile_mode(profile, index);
  if (!mode || !usb_cam_valid_interval(mode, interval)) { c.width = 0; return c; }
  c.width = mode->width; c.height = mode->height; c.quality = mode->quality;
  c.interval_100ns = interval;
  const uint32_t budget = uint32_t(uint64_t(USB_CAM_BYTE_BUDGET) * interval / 10000000);
  c.max_jpeg = budget < kMaxJpeg ? budget : kMaxJpeg;
  // UXGA can exceed the 15-FPS budget even at maximum allowed compression.
  // Accept frames that fit our existing slots instead of freezing indefinitely.
  // The actual delivered rate may fall below the requested rate for large JPEGs.
  if (USB_CAM_IS_UXGA(c.width, c.height)) c.max_jpeg = kMaxJpeg;
  return c;
}
} // namespace usb_cam
