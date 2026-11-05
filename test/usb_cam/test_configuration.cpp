#include <array>
#include <cassert>
#include <cstdio>
#include "usb_cam/configuration.h"

using namespace usb_cam;

int main() {
  Config config;
  assert(valid_config(config));
  assert(config_equal(config, mode_config(1, 333333)));
  std::array<uint8_t, kControlSize> bytes{};
  encode_config(config, bytes.data());
  Config decoded;
  assert(decode_config(bytes.data(), bytes.size(), decoded));
  assert(config_equal(config, decoded));
  for (size_t i = 0; i < bytes.size(); ++i) {
    bytes[i] ^= 1;
    assert(!decode_config(bytes.data(), bytes.size(), decoded));
    bytes[i] ^= 1;
  }
  assert(!decode_config(bytes.data(), bytes.size() - 1, decoded));
  assert(!decode_config(nullptr, bytes.size(), decoded));
  // Distinguish a broken return wire/header from corrupted payload or bad values.
  ConfigDecodeError error;
  assert(decode_config(bytes.data(), bytes.size(), decoded, &error));
  assert(error == ConfigDecodeError::None);
  bytes[0] = 0;
  assert(!decode_config(bytes.data(), bytes.size(), decoded, &error));
  assert(error == ConfigDecodeError::Magic);
  encode_config(config, bytes.data());
  bytes[24] ^= 1;
  assert(!decode_config(bytes.data(), bytes.size(), decoded, &error));
  assert(error == ConfigDecodeError::Checksum);
  bytes[24] = 99;
  put32(bytes.data() + 124, crc32(bytes.data(), 124));
  assert(!decode_config(bytes.data(), bytes.size(), decoded, &error));
  assert(error == ConfigDecodeError::Values);
  assert(!decode_config(nullptr, bytes.size(), decoded, &error));
  assert(error == ConfigDecodeError::Length);
  assert(config_equal(decoded, config)); // Failed decoding never changes output.
  struct InvalidField { size_t offset; ConfigDecodeError expected; };
  for (const auto& field : {InvalidField{4, ConfigDecodeError::Version},
                            InvalidField{6, ConfigDecodeError::EncodedLength},
                            InvalidField{26, ConfigDecodeError::Reserved},
                            InvalidField{123, ConfigDecodeError::Reserved}}) {
    encode_config(config, bytes.data());
    bytes[field.offset] ^= 1;
    put32(bytes.data() + 124, crc32(bytes.data(), 124));
    assert(!decode_config(bytes.data(), bytes.size(), decoded, &error));
    assert(error == field.expected && config_equal(decoded, config));
  }
  const uint32_t token = config_token(config);
  config.width = 800; config.height = 600;
  assert(valid_config(config));
  assert(config_token(config) != token); // Same generation after a Pico reset.
  config.width = 1920; config.height = 1080;
  assert(valid_config(config));
  config.width = 2048; config.height = 1536;
  assert(valid_config(config));
  config.width = 2560; config.height = 1920;
  assert(!valid_config(config));
  config = Config{};
  config.interval_100ns = USB_CAM_INTERVAL(120);
  assert(valid_config(config)); // Future Pico tuning must not need another ESP flash.
  config.interval_100ns--;
  assert(!valid_config(config));
  config.interval_100ns = 0;
  assert(!valid_config(config));
  config = Config{};
  config.max_jpeg = kMaxJpeg + 1;
  assert(!valid_config(config));
  config = Config{};
  config.xclk_mhz = 40;
  assert(!valid_config(config));
  config.xclk_mhz = 24;
  encode_config(config, bytes.data());
  assert(decode_config(bytes.data(), bytes.size(), decoded) && decoded.xclk_mhz == 24);
  config = Config{};
  for (unsigned i = 0; i < kSettingCount; ++i) {
    config.settings[i] = kSettingSpecs[i].minimum;
    assert(valid_config(config));
    config.settings[i] = kSettingSpecs[i].maximum;
    assert(valid_config(config));
    config.settings[i]++;
    assert(!valid_config(config));
    config.settings[i] = kDefaultSetting;
  }
  for (unsigned i = 1; i <= USB_CAM_MODE_COUNT; ++i) {
    const auto* mode = usb_cam_mode(i);
    assert(mode);
    for (unsigned n = 0; n < mode->interval_count; ++n) {
      config = mode_config(i, mode->intervals[n]);
      assert(valid_config(config));
      assert(config.max_jpeg <= kMaxJpeg);
      if (config.width == 1600 && config.height == 1200) {
        assert(config.max_jpeg == 65536);
      } else {
        assert(uint64_t(config.max_jpeg) * 10000000 <=
               uint64_t(USB_CAM_BYTE_BUDGET) * config.interval_100ns);
      }
    }
  }
  assert(!usb_cam_mode(0) && !usb_cam_mode(USB_CAM_MODE_COUNT + 1));
  config = Config{};
  encode_config(config, bytes.data());
  for (size_t i = 76; i < 124; ++i) assert(bytes[i] == 0); // Legacy token unchanged.
  // Optional PLL extension: multiplier, sys divider, pre divider, root 2x,
  // seld5 and manual PCLK divider. 20 MHz -> VCO 200 / SYS 50 / PCLK 10 MHz.
  bytes[76] = 30; bytes[77] = 1; bytes[78] = 3; bytes[81] = 10;
  put32(bytes.data() + 124, crc32(bytes.data(), 124));
  assert(decode_config(bytes.data(), bytes.size(), decoded));
  bytes[76] = 15; // VCO 100 MHz is outside the datasheet operating range.
  put32(bytes.data() + 124, crc32(bytes.data(), 124));
  assert(!decode_config(bytes.data(), bytes.size(), decoded));
  config = Config{};
  config.pll.multiplier = 15; config.pll.pre_div = 2; config.pll.pclk_div = 9;
  assert(valid_config(config)); // VCO 150 MHz lower bound.
  config.pll.multiplier = 14;
  assert(!valid_config(config));
  config = Config{}; config.pll.multiplier = 16; config.pll.root_2x = 1; config.pll.pclk_div = 11;
  assert(!valid_config(config)); // Pinned SDK writes the reserved bit instead of the root-divider bit.
  config.pll.root_2x = 0; config.pll.pre_div = 1;
  assert(valid_config(config)); // Documented predivider: VCO 213.3 / SYS 53.3 / PCLK 9.70 MHz.
  config.pll.multiplier = 10; config.pll.pre_div = 0; config.pll.pclk_div = 10;
  assert(!valid_config(config)); // 20 MHz PLL reference exceeds 13.5 MHz.
  config = Config{}; config.pll.multiplier = 30; config.pll.pclk_div = 30;
  assert(!valid_config(config)); // This low PCLK caused capture timeouts on hardware.
  config = Config{}; config.xclk_mhz = 24;
  config.pll.multiplier = 27; config.pll.pclk_div = 11;
  assert(valid_config(config)); // SYSCLK 54 MHz upper bound.
  config.pll.multiplier = 28;
  assert(!valid_config(config));
  config.pll.multiplier = 27; config.pll.pclk_div = 10;
  assert(!valid_config(config)); // PCLK 10.8 MHz is too high for this capture path.
  for (const auto p : {USB_CAM_PROFILE_DISCOVERY, USB_CAM_PROFILE_OV2640, USB_CAM_PROFILE_OV3660_LEGACY}) {
    const auto defaults = profile_defaults(p);
    assert(defaults.xclk_mhz == 20 && !defaults.pll.multiplier);
  }
  const auto calibrated = profile_defaults(USB_CAM_PROFILE_OV3660);
  assert(valid_config(calibrated) && calibrated.xclk_mhz == 20 && calibrated.pll.multiplier == 16 &&
         calibrated.pll.pre_div == 1 && !calibrated.pll.root_2x && calibrated.pll.pclk_div == 11);
  const auto ov3660_uxga = mode_config(9, USB_CAM_INTERVAL(15), USB_CAM_PROFILE_OV3660);
  assert(ov3660_uxga.quality == 28 && ov3660_uxga.pll.multiplier == 16 && ov3660_uxga.max_jpeg == 65536);
  assert(!usb_cam_valid_interval(usb_cam_profile_mode(USB_CAM_PROFILE_OV3660, 1), USB_CAM_INTERVAL(60)));
  assert(!usb_cam_valid_interval(usb_cam_profile_mode(USB_CAM_PROFILE_OV3660, 4), USB_CAM_INTERVAL(60)));
  assert(usb_cam_valid_interval(usb_cam_profile_mode(USB_CAM_PROFILE_OV3660, 3), USB_CAM_INTERVAL(20)));
  assert(!usb_cam_valid_interval(usb_cam_profile_mode(USB_CAM_PROFILE_OV3660, 3), USB_CAM_INTERVAL(30)));
  assert(!usb_cam_valid_interval(usb_cam_profile_mode(USB_CAM_PROFILE_OV3660, 10), USB_CAM_INTERVAL(20)));
  assert(usb_cam_valid_interval(usb_cam_profile_mode(USB_CAM_PROFILE_OV3660, 10), USB_CAM_INTERVAL(15)));
  assert(usb_cam_profile_for(0, 0) == USB_CAM_PROFILE_DISCOVERY);
  assert(usb_cam_profile_for(0x26, 0) == USB_CAM_PROFILE_OV2640);
  assert(usb_cam_profile_for(0x3660, 0) == USB_CAM_PROFILE_OV3660_LEGACY);
  assert(usb_cam_profile_for(0x3660, kSensorProfilesCapability) == USB_CAM_PROFILE_OV3660);
  assert(usb_cam_profile_for(0x5640, kSensorProfilesCapability) == USB_CAM_PROFILE_DISCOVERY);
  assert(!usb_cam_profile_mode(USB_CAM_PROFILE_DISCOVERY, 1));
  assert(!usb_cam_profile_mode(USB_CAM_PROFILE_OV2640, 10));
  assert(!valid_config(mode_config(11, USB_CAM_INTERVAL(5), USB_CAM_PROFILE_OV3660)));
  config = mode_config(10, USB_CAM_INTERVAL(5), USB_CAM_PROFILE_OV3660);
  assert(valid_config(config) && config.width == 1920 && config.height == 1080 && config.max_jpeg == 65536);
  assert(!valid_config(mode_config(11, USB_CAM_INTERVAL(5), USB_CAM_PROFILE_OV3660_LEGACY)));
  puts("Configuration tests passed: CRC, bounds, settings, reset tokens and all modes.");
}
