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
  kConfigGeometry = 4, kConfigQuality = 5, kConfigFrame = 6
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
struct Config {
  uint32_t generation = 1;
  uint16_t width = 640, height = 480;
  uint32_t interval_100ns = USB_CAM_INTERVAL(30);
  uint32_t max_jpeg = uint32_t(uint64_t(USB_CAM_BYTE_BUDGET) * USB_CAM_INTERVAL(30) / 10000000);
  uint8_t quality = 16, xclk_mhz = 20;
  int16_t settings[kSettingCount];
  Config() { for (auto& value : settings) value = kDefaultSetting; }
};
inline bool supported_geometry(uint16_t w, uint16_t h) {
  // All SDK geometries through UXGA, independent of the Pico's menu.
  static const uint16_t sizes[][2] = {{96,96},{160,120},{176,144},{240,176},{240,240},
    {320,240},{400,296},{480,320},{640,480},{800,600},{1024,768},{1280,720},
    {1280,1024},{1600,1200}};
  for (const auto& size : sizes) if (size[0] == w && size[1] == h) return true;
  return false;
}
inline bool valid_config(const Config& c) {
  if (!supported_geometry(c.width, c.height) || c.interval_100ns < USB_CAM_INTERVAL(60) ||
      c.interval_100ns > 10000000 || c.max_jpeg < 1024 || c.max_jpeg > kMaxJpeg ||
      c.quality > 50 || (c.xclk_mhz != 10 && c.xclk_mhz != 16 && c.xclk_mhz != 20)) return false;
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
  for (size_t i = 28 + 2 * kSettingCount; i < 124; ++i) {
    if (data[i]) return fail(ConfigDecodeError::Reserved);
  }
  Config c;
  c.generation = get32(data + 8); c.width = get16(data + 12); c.height = get16(data + 14);
  c.interval_100ns = get32(data + 16); c.max_jpeg = get32(data + 20);
  c.quality = data[24]; c.xclk_mhz = data[25];
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
inline Config mode_config(unsigned index, uint32_t interval) {
  Config c;
  const UsbCamMode* mode = usb_cam_mode(index);
  if (!mode || !usb_cam_valid_interval(mode, interval)) { c.width = 0; return c; }
  c.width = mode->width; c.height = mode->height; c.quality = mode->quality;
  c.interval_100ns = interval;
  const uint32_t budget = uint32_t(uint64_t(USB_CAM_BYTE_BUDGET) * interval / 10000000);
  c.max_jpeg = budget < kMaxJpeg ? budget : kMaxJpeg;
  return c;
}
} // namespace usb_cam
