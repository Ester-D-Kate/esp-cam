#pragma once

#include <cstddef>
#include <cstdint>
#include "profile.h"

namespace usb_cam {
constexpr size_t kHeaderSize = 48;
constexpr size_t kPacketCapacity = kHeaderSize + kProfile.max_jpeg;
constexpr uint32_t kSpiHz = 8000000;

struct Header {
  uint32_t sequence = 0;
  uint32_t timestamp_us = 0;
  uint32_t payload_size = 0;
  uint32_t payload_crc = 0;
  uint32_t capture_errors = 0;
  uint32_t oversized = 0;
  uint32_t link_timeouts = 0;
  uint8_t quality = kProfile.quality;
};

inline uint32_t crc32(const uint8_t* data, size_t length) {
  // Reflected IEEE CRC32; a nibble table is fast on both MCUs and uses 64 bytes.
  static constexpr uint32_t table[16] = {
      0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
      0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
      0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
      0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c};
  uint32_t crc = 0xffffffff;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    crc = (crc >> 4) ^ table[crc & 15];
    crc = (crc >> 4) ^ table[crc & 15];
  }
  return ~crc;
}

inline void put32(uint8_t* data, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) data[i] = uint8_t(value >> (8 * i));
}
inline uint32_t get32(const uint8_t* data) {
  return uint32_t(data[0]) | (uint32_t(data[1]) << 8) |
         (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
}
inline uint16_t big16(const uint8_t* data) {
  return uint16_t((uint16_t(data[0]) << 8) | data[1]);
}

inline void encode_header(const Header& header, uint8_t* out) {
  out[0] = 'P'; out[1] = 'O'; out[2] = 'V'; out[3] = '1';
  out[4] = 1; out[5] = 0;
  out[6] = uint8_t(kHeaderSize); out[7] = 0;
  put32(out + 8, USB_CAM_PROFILE);
  put32(out + 12, header.sequence);
  put32(out + 16, header.timestamp_us);
  put32(out + 20, header.payload_size);
  put32(out + 24, header.payload_crc);
  put32(out + 28, header.capture_errors);
  put32(out + 32, header.oversized);
  put32(out + 36, header.link_timeouts);
  out[40] = header.quality;
  out[41] = out[42] = out[43] = 0;
  put32(out + 44, crc32(out, 44));
}

inline bool decode_header(const uint8_t* data, size_t length, Header& out) {
  if (!data || length < kHeaderSize || get32(data) != 0x31564f50 ||
      data[4] != 1 || data[5] != 0 || data[6] != kHeaderSize || data[7] != 0 ||
      get32(data + 8) != USB_CAM_PROFILE || data[40] > 63 ||
      data[41] != 0 || data[42] != 0 || data[43] != 0 ||
      get32(data + 44) != crc32(data, 44)) return false;
  const uint32_t payload_size = get32(data + 20);
  if (payload_size < 4 || payload_size > kProfile.max_jpeg) return false;
  out.sequence = get32(data + 12);
  out.timestamp_us = get32(data + 16);
  out.payload_size = payload_size;
  out.payload_crc = get32(data + 24);
  out.capture_errors = get32(data + 28);
  out.oversized = get32(data + 32);
  out.link_timeouts = get32(data + 36);
  out.quality = data[40];
  return true;
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

inline bool validate_payload(const Header& header, const uint8_t* data, size_t length) {
  if (length != header.payload_size || length > kProfile.max_jpeg ||
      crc32(data, length) != header.payload_crc) return false;
  size_t jpeg_length = length;
  return validate_jpeg(data, jpeg_length, kProfile.width, kProfile.height) && jpeg_length == length;
}
}  // namespace usb_cam
