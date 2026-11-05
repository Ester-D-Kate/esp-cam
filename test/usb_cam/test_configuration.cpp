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
  assert(!valid_config(config));
  config = Config{};
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
  puts("Configuration tests passed: CRC, bounds, settings, reset tokens and all modes.");
}
