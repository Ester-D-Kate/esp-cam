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
constexpr uint32_t kFullModesCapability = USB_CAM_CAP_FULL_MODES;
#define USB_CAM_READOUT_FIELDS(X) \
  X(start_x) X(start_y) X(end_x) X(end_y) X(offset_x) X(offset_y) X(total_x) X(total_y)
struct Readout {
  bool enabled = false, binning = false, scale = true;
  uint8_t x_increment = 0x11, y_increment = 0x11;
  uint16_t start_x = 0, start_y = 0, end_x = 2079, end_y = 1547;
  uint16_t offset_x = 16, offset_y = 6, total_x = 2300, total_y = 1564;
};
inline bool readout_equal(const Readout& a, const Readout& b) {
  if (a.enabled != b.enabled) return false;
  if (!a.enabled) return true;
  if (a.binning != b.binning || a.scale != b.scale || a.x_increment != b.x_increment ||
      a.y_increment != b.y_increment) return false;
#define USB_CAM_COMPARE_READOUT(field) if (a.field != b.field) return false;
  USB_CAM_READOUT_FIELDS(USB_CAM_COMPARE_READOUT)
#undef USB_CAM_COMPARE_READOUT
  return true;
}
struct SensorPll {
  uint8_t multiplier = 0; // Zero preserves SDK timing and the legacy wire token.
  uint8_t sys_div = 1, pre_div = 3, root_2x = 0, seld5 = 0, pclk_div = 10;
};
inline bool valid_pll(const SensorPll& p, unsigned xclk_mhz) {
  // The pinned OV3660 callback writes root_2x into reserved bit 6 of 0x303D,
  // while the datasheet places the root divider at bit 2. Keep it disabled.
  if (p.root_2x) return false;
  if (!p.multiplier) return true;
  if (p.multiplier > 31 || !p.sys_div || p.sys_div > 15 || p.pre_div > 3 ||
      p.root_2x > 1 || p.seld5 > 3 || !p.pclk_div || p.pclk_div > 31) return false;
  constexpr unsigned pre2[] = {2, 3, 4, 6}, seld2[] = {2, 2, 4, 5};
  // Rational comparisons avoid rounding an out-of-range clock into acceptance.
  const uint64_t reference_n = uint64_t(xclk_mhz) * 1000000 * 2;
  const uint64_t vco_n = reference_n * p.multiplier;
  const uint64_t vco_d = pre2[p.pre_div];
  const uint64_t pll_d = vco_d * p.sys_div * seld2[p.seld5];
  return reference_n >= 4000000u * vco_d && reference_n <= 13500000u * vco_d &&
      vco_n >= 150000000u * vco_d && vco_n <= 500000000u * vco_d &&
      vco_n * 2 <= 54000000u * pll_d * 4 &&
      vco_n * 2 >= 8000000u * pll_d * 2 * p.pclk_div &&
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
  Readout readout;
  int16_t settings[kSettingCount];
  Config() { for (auto& value : settings) value = kDefaultSetting; }
};
inline unsigned ov3660_rate_limit(unsigned w, unsigned h) {
  if (w <= 320 && h <= 240) return 120;
  if (w <= 640 && h <= 480) return 60;
  if (w <= 1280 && h <= 768) return 45;
  return w == 1920 && h == 1080 ? 20 : 15;
}
inline uint32_t sysclk_hz(const Config& c) {
  constexpr unsigned pre2[] = {2,3,4,6}, seld2[] = {2,2,4,5};
  if (!c.pll.multiplier || !valid_pll(c.pll,c.xclk_mhz)) return 0;
  return uint32_t(uint64_t(c.xclk_mhz) * 1000000u * c.pll.multiplier /
      (pre2[c.pll.pre_div] * c.pll.sys_div * seld2[c.pll.seld5]));
}
inline bool valid_readout(const Config& c) {
  const auto& r = c.readout;
  if (!r.enabled) return true;
  const uint32_t clock = sysclk_hz(c);
  if (!clock || r.start_x >= r.end_x || r.start_y >= r.end_y || r.end_x > 2079 || r.end_y > 1547 ||
      (r.start_x & 1) || (r.start_y & 1) || !(r.end_x & 1) || !(r.end_y & 1) ||
      r.x_increment != r.y_increment ||
      (r.x_increment != 0x11 && r.x_increment != 0x31 && r.x_increment != 0x71) ||
      r.binning != (r.x_increment != 0x11)) return false;
  const unsigned divisor = ((r.x_increment >> 4) + (r.x_increment & 15)) / 2;
  const unsigned span_x = r.end_x - r.start_x + 1, span_y = r.end_y - r.start_y + 1;
  if (span_x % divisor || span_y % divisor) return false;
  const unsigned input_x = span_x / divisor, input_y = span_y / divisor;
  if (input_x < c.width + 2u*r.offset_x || input_y < c.height + 2u*r.offset_y ||
      r.total_x < input_x + 32 || r.total_y < input_y + 8 || r.total_x > 4095 || r.total_y > 4095)
    return false;
  const unsigned image_x = input_x - 2*r.offset_x, image_y = input_y - 2*r.offset_y;
  if (image_x*c.height != image_y*c.width || (!r.scale && (image_x != c.width || image_y != c.height)) ||
      uint64_t(clock) > uint64_t(ov3660_rate_limit(c.width,c.height))*r.total_x*r.total_y ||
      clock/r.total_x/100 > 1023 || ! (clock/r.total_x/120)) return false;
  const int exposure = c.settings[unsigned(Setting::AecValue)];
  return c.settings[unsigned(Setting::Aec2)] != 1 &&
      (exposure == kDefaultSetting || exposure <= int(r.total_y)-4);
}
inline Config profile_defaults(UsbCamProfile profile) {
  Config c;
  if (usb_cam_full_profile(profile)) c.max_jpeg=usb_cam_profile_capacity(profile);
  if (profile == USB_CAM_PROFILE_OV2640_FULL) c.xclk_mhz = 24;
  if (profile == USB_CAM_PROFILE_OV3660 || profile == USB_CAM_PROFILE_OV3660_FULL) {
    // Hardware-validated: 20 MHz XCLK, 53.33 MHz SYSCLK, 9.70 MHz PCLK.
    c.pll.multiplier = 16;
    c.pll.pre_div = 1;
    c.pll.pclk_div = 11;
  }
  return c;
}
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
  if (!supported_geometry(c.width, c.height) || c.interval_100ns < USB_CAM_INTERVAL(120) ||
      c.interval_100ns > 10000000 || c.max_jpeg < 1024 || c.max_jpeg > kMaxJpeg ||
      c.quality > 50 || !supported_xclk(c.xclk_mhz) || !valid_pll(c.pll, c.xclk_mhz) || !valid_readout(c)) return false;
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
  if (c.readout.enabled) {
    const auto& r = c.readout;
    out[82] = 1u | (r.binning ? 2u : 0u) | (r.scale ? 4u : 0u);
    out[83] = r.x_increment; out[84] = r.y_increment;
    unsigned offset = 86;
#define USB_CAM_ENCODE_READOUT(field) put16(out+offset,r.field); offset += 2;
    USB_CAM_READOUT_FIELDS(USB_CAM_ENCODE_READOUT)
#undef USB_CAM_ENCODE_READOUT
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
  if ((data[82] & ~7u) || data[85]) return fail(ConfigDecodeError::Reserved);
  if (!(data[82] & 1u)) for (size_t i = 82; i < 102; ++i) if (data[i]) return fail(ConfigDecodeError::Reserved);
  for (size_t i = 102; i < 124; ++i) {
    if (data[i]) return fail(ConfigDecodeError::Reserved);
  }
  Config c;
  c.generation = get32(data + 8); c.width = get16(data + 12); c.height = get16(data + 14);
  c.interval_100ns = get32(data + 16); c.max_jpeg = get32(data + 20);
  c.quality = data[24]; c.xclk_mhz = data[25];
  if (data[76]) {
    c.pll.multiplier = data[76]; c.pll.sys_div = data[77]; c.pll.pre_div = data[78];
    c.pll.root_2x = data[79]; c.pll.seld5 = data[80]; c.pll.pclk_div = data[81];
  }
  if (data[82] & 1u) {
    auto& r = c.readout;
    r.enabled = true; r.binning = data[82] & 2u; r.scale = data[82] & 4u;
    r.x_increment = data[83]; r.y_increment = data[84];
    unsigned offset = 86;
#define USB_CAM_DECODE_READOUT(field) r.field = get16(data+offset); offset += 2;
    USB_CAM_READOUT_FIELDS(USB_CAM_DECODE_READOUT)
#undef USB_CAM_DECODE_READOUT
  }
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
inline unsigned greatest_common_divisor(unsigned a, unsigned b) {
  while (b) { const unsigned remainder = a % b; a = b; b = remainder; }
  return a;
}
inline Readout mode_readout(uint16_t width, uint16_t height, uint32_t interval) {
  Readout r;
  unsigned max_width=0, max_height=0, divisor=1;
  if (width <= 640 && height <= 480 && interval < USB_CAM_INTERVAL(30)) {
    const bool fast=interval < USB_CAM_INTERVAL(60);
    max_width=fast ? 320 : 640; max_height=fast ? 240 : 480;
    divisor=fast ? 4 : 2; r.offset_x=fast ? 4 : 8; r.offset_y=fast ? 1 : 3;
    r.total_x=1600; r.total_y=fast ? 278 : 556;
  } else if (width <= 1024 && height <= 768 && interval < USB_CAM_INTERVAL(30)) {
    // The full-array binned candidate timed out on hardware at this HTS.
    // Use the verified native crop; fast modes have a narrower field of view.
    max_width=1024; max_height=768;
    r.total_x=1512; r.total_y=800;
  } else if (width == 1280 && height == 720 && interval < USB_CAM_INTERVAL(20)) {
    max_width=1280; max_height=720;
    r.total_x=1512; r.total_y=784;
  } else if (width == 1920 && height == 1080 && interval < USB_CAM_INTERVAL(15)) {
    max_width=1920; max_height=1080;
    r.total_x=2080; r.total_y=1283;
  } else return r;
  const unsigned gcd=greatest_common_divisor(width,height), unit_x=width/gcd, unit_y=height/gcd;
  unsigned units=max_width/unit_x;
  if (max_height/unit_y < units) units=max_height/unit_y;
  // Center on even sensor coordinates and preserve the requested aspect ratio.
  const unsigned alignment=4/divisor;
  units=units/alignment*alignment;
  const unsigned span_x=(units*unit_x+2*r.offset_x)*divisor;
  const unsigned span_y=(units*unit_y+2*r.offset_y)*divisor;
  r.start_x=(2080-span_x)/2; r.start_y=(1548-span_y)/2;
  r.end_x=r.start_x+span_x-1; r.end_y=r.start_y+span_y-1;
  r.enabled=true; r.binning=divisor!=1;
  r.x_increment=r.y_increment=uint8_t(((2*divisor-1)<<4)|1);
  return r;
}
inline Config mode_config(unsigned index, uint32_t interval, UsbCamProfile profile = USB_CAM_PROFILE_OV2640) {
  Config c = profile_defaults(profile);
  const UsbCamMode* mode = usb_cam_profile_mode(profile, index);
  if (!mode || !usb_cam_valid_interval(mode, interval)) { c.width = 0; return c; }
  c.width = mode->width; c.height = mode->height; c.quality = mode->quality;
  c.interval_100ns = interval;
  const uint32_t budget = uint32_t(uint64_t(USB_CAM_BYTE_BUDGET) * interval / 10000000);
  const uint32_t capacity=usb_cam_profile_capacity(profile);
  c.max_jpeg = budget < capacity ? budget : capacity;
  // On hardware, HD45 exceeded its 16 KiB rate estimate even at quality 50,
  // discarding every frame. Full profiles accept any JPEG that fits the slot;
  // backpressure reduces delivered FPS when a scene exceeds link bandwidth.
  // Preserve the previous limits for legacy ESP firmware/profile banks.
  if (usb_cam_full_profile(profile) || USB_CAM_USES_FULL_SLOT(c.width, c.height)) c.max_jpeg = capacity;
  if (profile == USB_CAM_PROFILE_OV3660_FULL) c.readout=mode_readout(c.width,c.height,interval);
  return c;
}
} // namespace usb_cam
