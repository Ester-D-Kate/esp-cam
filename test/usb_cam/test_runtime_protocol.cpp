#include <array>
#include <cassert>
#include <cstdio>
#include "usb_cam/protocol.h"

using namespace usb_cam;
int main() {
  Header status;
  status.kind = PacketKind::Status;
  status.config_error = 3;
  status.attempted_generation = 7;
  std::array<uint8_t, kHeaderSize> bytes{};
  encode_header(status, bytes.data());
  Header decoded;
  assert(decode_header(bytes.data(), bytes.size(), decoded));
  assert(decoded.kind == PacketKind::Status && decoded.config_error == 3);
  assert(decoded.attempted_generation == 7);
  // Status must preserve the receiver's evidence even when CFG2 is rejected.
  put32(bytes.data() + 72, 5); // Control checksum error.
  put32(bytes.data() + 76, 0x11223344);
  put32(bytes.data() + 80, 0x55667788);
  memcpy(bytes.data() + 84, "bad-control-data", 16);
  put32(bytes.data() + 100, 1000000);
  put32(bytes.data() + 104, 1); // Camera driver initialization attempts.
  put32(bytes.data() + 108, 2); // In-place sensor reset attempts.
  put32(bytes.data() + 112, 24003750); // Actual LEDC clock, not just its request.
  put32(bytes.data() + 124, crc32(bytes.data(), 124));
  assert(decode_header(bytes.data(), bytes.size(), decoded));
  std::array<uint8_t, kHeaderSize> roundtrip{};
  encode_header(decoded, roundtrip.data());
  assert(roundtrip == bytes);
  bytes[120] = 1; // The remaining reserved area is still protected.
  put32(bytes.data() + 124, crc32(bytes.data(), 124));
  assert(!decode_header(bytes.data(), bytes.size(), decoded));
  Config config;
  assert(!matches_config(status, config));
  Header frame;
  frame.kind = PacketKind::Frame;
  frame.width = config.width; frame.height = config.height;
  frame.interval_100ns = config.interval_100ns;
  frame.generation = config.generation; frame.config_token = config_token(config);
  frame.payload_size = 100;
  assert(matches_config(frame, config));
  config.generation++;
  assert(!matches_config(frame, config));
  config.generation--;
  config.quality++;
  assert(!matches_config(frame, config)); // Same generation, different boot config.
  config.quality--;
  frame.width++;
  assert(!matches_config(frame, config));
  frame.width--;
  frame.config_error = 2;
  assert(!matches_config(frame, config));
  encode_header(frame, bytes.data());
  assert(!decode_header(bytes.data(), bytes.size(), decoded));
  frame.config_error = 0;
  encode_header(frame, bytes.data());
  assert(decode_header(bytes.data(), bytes.size(), decoded));
  for (size_t i = 0; i < bytes.size(); ++i) {
    bytes[i] ^= 1;
    assert(!decode_header(bytes.data(), bytes.size(), decoded));
    bytes[i] ^= 1;
  }
  status.capabilities = kSensorProfilesCapability;
  encode_header(status, bytes.data());
  assert(decode_header(bytes.data(), bytes.size(), decoded));
  assert(decoded.capabilities == kSensorProfilesCapability);
  puts("Runtime protocol tests passed: status, errors and mode/configuration fencing.");
}
