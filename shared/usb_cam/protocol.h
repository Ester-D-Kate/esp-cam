#pragma once
#include "configuration.h"

namespace usb_cam {
constexpr size_t kHeaderSize = kControlSize;
constexpr size_t kPacketCapacity = kHeaderSize + kMaxJpeg;
constexpr uint32_t kSpiHz = 8000000;
constexpr uint32_t kControlSpiHz = 1000000;
enum class PacketKind : uint8_t { Status = 0, Frame = 1 };
struct Header {
  PacketKind kind = PacketKind::Status;
  uint32_t sequence = 0, timestamp_us = 0, payload_size = 0, payload_crc = 0;
  uint32_t capture_errors = 0, oversized = 0, link_timeouts = 0;
  uint8_t quality = 16;
  uint16_t width = 640, height = 480, sensor_pid = 0;
  uint32_t interval_100ns = USB_CAM_INTERVAL(30);
  uint32_t generation = 0, config_token = 0;
  uint32_t attempted_generation = 0, attempted_token = 0;
  uint32_t config_error = 0, command_errors = 0;
  ConfigDecodeError control_error = ConfigDecodeError::None;
  uint32_t control_crc_received = 0, control_crc_calculated = 0;
  uint8_t control_prefix[16]{};
  uint32_t control_spi_hz = 0;
  uint32_t camera_starts = 0, sensor_resets = 0, xclk_hz = 0;
};
inline void encode_header(const Header& h, uint8_t* out) {
  memset(out, 0, kHeaderSize);
  put32(out, 0x32564f50); // POV2
  put16(out + 4, 2); put16(out + 6, kHeaderSize);
  out[8] = uint8_t(h.kind); out[9] = h.quality;
  put16(out + 10, h.sensor_pid);
  put32(out + 12, h.sequence); put32(out + 16, h.timestamp_us);
  put32(out + 20, h.payload_size); put32(out + 24, h.payload_crc);
  put32(out + 28, h.capture_errors); put32(out + 32, h.oversized);
  put32(out + 36, h.link_timeouts);
  put16(out + 40, h.width); put16(out + 42, h.height);
  put32(out + 44, h.interval_100ns); put32(out + 48, h.generation);
  put32(out + 52, h.config_token); put32(out + 56, h.attempted_generation);
  put32(out + 60, h.attempted_token); put32(out + 64, h.config_error);
  put32(out + 68, h.command_errors);
  put32(out + 72, uint32_t(h.control_error));
  put32(out + 76, h.control_crc_received); put32(out + 80, h.control_crc_calculated);
  memcpy(out + 84, h.control_prefix, sizeof(h.control_prefix));
  put32(out + 100, h.control_spi_hz);
  put32(out + 104, h.camera_starts); put32(out + 108, h.sensor_resets);
  put32(out + 112, h.xclk_hz);
  put32(out + 124, crc32(out, 124));
}
inline bool decode_header(const uint8_t* data, size_t length, Header& out) {
  if (!data || length < kHeaderSize || get32(data) != 0x32564f50 ||
      get16(data + 4) != 2 || get16(data + 6) != kHeaderSize ||
      data[8] > 1 || data[9] > 63 || get32(data + 124) != crc32(data, 124)) return false;
  if (get32(data + 72) > uint32_t(ConfigDecodeError::Values)) return false;
  for (size_t i = 116; i < 124; ++i) if (data[i]) return false;
  Header h;
  h.kind = PacketKind(data[8]); h.quality = data[9]; h.sensor_pid = get16(data + 10);
  h.sequence = get32(data + 12); h.timestamp_us = get32(data + 16);
  h.payload_size = get32(data + 20); h.payload_crc = get32(data + 24);
  h.capture_errors = get32(data + 28); h.oversized = get32(data + 32);
  h.link_timeouts = get32(data + 36);
  h.width = get16(data + 40); h.height = get16(data + 42);
  h.interval_100ns = get32(data + 44); h.generation = get32(data + 48);
  h.config_token = get32(data + 52); h.attempted_generation = get32(data + 56);
  h.attempted_token = get32(data + 60); h.config_error = get32(data + 64);
  h.command_errors = get32(data + 68);
  h.control_error = ConfigDecodeError(get32(data + 72));
  h.control_crc_received = get32(data + 76); h.control_crc_calculated = get32(data + 80);
  memcpy(h.control_prefix, data + 84, sizeof(h.control_prefix));
  h.control_spi_hz = get32(data + 100);
  h.camera_starts = get32(data + 104); h.sensor_resets = get32(data + 108);
  h.xclk_hz = get32(data + 112);
  if (h.kind == PacketKind::Status) {
    if (h.payload_size || h.payload_crc) return false;
  } else if (h.config_error || h.payload_size < 4 || h.payload_size > kMaxJpeg ||
             !supported_geometry(h.width, h.height) ||
             h.interval_100ns < USB_CAM_INTERVAL(60) || h.interval_100ns > 10000000) return false;
  out = h;
  return true;
}
inline bool matches_config(const Header& h, const Config& c) {
  return h.kind == PacketKind::Frame && !h.config_error &&
      h.generation == c.generation && h.config_token == config_token(c) &&
      h.width == c.width && h.height == c.height && h.interval_100ns == c.interval_100ns &&
      h.payload_size >= 4 && h.payload_size <= c.max_jpeg;
}
inline bool validate_jpeg(const uint8_t* data, size_t& length,
                          uint16_t width, uint16_t height) {
  if (!data || length < 4 || data[0] != 0xff || data[1] != 0xd8) return false;
  size_t end = length;
  const size_t search_end = length > 16 ? length - 16 : 2;
  while (end > search_end && (data[end - 2] != 0xff || data[end - 1] != 0xd9)) --end;
  if (end <= search_end) return false;
  bool geometry = false;
  size_t offset = 2;
  while (offset + 1 < end) {
    if (data[offset++] != 0xff) return false;
    while (offset < end && data[offset] == 0xff) ++offset;
    if (offset >= end) return false;
    const uint8_t marker = data[offset++];
    if (marker == 0 || marker == 0xd8 || marker == 0xd9) return false;
    if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;
    if (end - offset < 2) return false;
    const size_t segment = big16(data + offset);
    if (segment < 2 || segment > end - offset) return false;
    if (marker == 0xc0) {
      if (segment < 8 || data[offset + 2] != 8 ||
          segment != size_t(8 + 3 * data[offset + 7]) ||
          big16(data + offset + 3) != height || big16(data + offset + 5) != width) return false;
      geometry = true;
    } else if (marker == 0xda) {
      if (!geometry || segment < 6 || offset + segment >= end - 2) return false;
      length = end;
      return true;
    }
    offset += segment;
  }
  return false;
}

inline bool validate_payload(const Header& h, const uint8_t* data, size_t length) {
  if (h.kind != PacketKind::Frame || !data || length != h.payload_size || length > kMaxJpeg ||
      crc32(data, length) != h.payload_crc) return false;
  size_t jpeg_length = length;
  return validate_jpeg(data, jpeg_length, h.width, h.height) && jpeg_length == length;
}
} // namespace usb_cam
