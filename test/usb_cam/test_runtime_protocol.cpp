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
  puts("Runtime protocol tests passed: status, errors and mode/configuration fencing.");
}
