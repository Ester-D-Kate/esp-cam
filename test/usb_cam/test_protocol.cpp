#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "usb_cam/protocol.h"
#include "usb_cam/frame_slots.h"
#include "usb_cam/frame_clock.h"

using namespace usb_cam;

static std::vector<uint8_t> jpeg(uint16_t width, uint16_t height) {
  return {0xff, 0xd8, 0xff, 0xe0, 0, 4, 0, 0,
          0xff, 0xc0, 0, 11, 8, uint8_t(height >> 8), uint8_t(height),
          uint8_t(width >> 8), uint8_t(width), 1, 1, 0x11, 0,
          0xff, 0xda, 0, 8, 1, 1, 0, 0, 63, 0, 0x42, 0xff, 0xd9};
}

static void test_crc_and_frame_validation() {
  assert(crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xcbf43926);
  assert(crc32(nullptr, 0) == 0);
  auto payload = jpeg(640, 480);
  size_t length = payload.size();
  assert(validate_jpeg(payload.data(), length, 640, 480));
  payload.push_back(0);  // Sensor DMA may leave padding after the EOI marker.
  length = payload.size();
  assert(validate_jpeg(payload.data(), length, 640, 480));
  assert(length == payload.size() - 1);
  assert(!validate_jpeg(payload.data(), length, 1, 1));
  payload.resize(length);

  Header source{};
  source.kind = PacketKind::Frame;
  source.sequence = 0xffffffff;
  source.timestamp_us = 0xfffffff0;
  source.payload_size = uint32_t(payload.size());
  source.payload_crc = crc32(payload.data(), payload.size());
  source.quality = 16;
  std::array<uint8_t, kHeaderSize> wire{};
  encode_header(source, wire.data());
  assert(wire[0] == 'P' && wire[1] == 'O' && wire[2] == 'V' && wire[3] == '2');
  Header decoded{};
  assert(decode_header(wire.data(), wire.size(), decoded));
  assert(decoded.sequence == source.sequence && decoded.timestamp_us == source.timestamp_us);
  assert(decoded.payload_size == payload.size());
  assert(validate_payload(decoded, payload.data(), payload.size()));
  for (size_t i = 0; i < wire.size(); ++i) {
    wire[i] ^= 0x80;
    assert(!decode_header(wire.data(), wire.size(), decoded));
    wire[i] ^= 0x80;
  }
  assert(!decode_header(wire.data(), wire.size() - 1, decoded));
  assert(decode_header(wire.data(), wire.size(), decoded));
  payload[30] ^= 1;
  assert(!validate_payload(decoded, payload.data(), payload.size()));
  payload[30] ^= 1;
  assert(!validate_payload(decoded, payload.data(), payload.size() - 1));
  source.payload_size = kMaxJpeg + 1;
  encode_header(source, wire.data());
  assert(!decode_header(wire.data(), wire.size(), decoded));
  source.payload_size = 0;
  encode_header(source, wire.data());
  assert(!decode_header(wire.data(), wire.size(), decoded));
}

static void test_jpeg_bounds() {
  auto payload = jpeg(640, 480);
  for (size_t n = 0; n < payload.size(); ++n) {
    size_t length = n;
    assert(!validate_jpeg(payload.data(), length, 640, 480));
  }
  payload[4] = 0xff;
  payload[5] = 0xff;
  size_t length = payload.size();
  assert(!validate_jpeg(payload.data(), length, 640, 480));
}

static void test_slow_usb_ownership() {
  FrameSlots slots;
  bool replaced = false;
  int a = slots.begin_receive(replaced);
  assert(a >= 0 && !replaced);
  slots.received(a);
  assert(slots.begin_validate() == a);
  slots.publish(a);
  assert(slots.take_latest() == a);
  int b = slots.begin_receive(replaced);
  assert(b >= 0 && b != a);
  slots.received(b);
  assert(slots.begin_validate() == b);
  slots.publish(b);
  int next = slots.begin_receive(replaced);
  assert(next == -1 && !replaced); // Preserve the only complete frame while USB sends A.
  assert(slots.state(a) == SlotState::Sending);
  slots.release(a);
  // USB completes before the next CS edge: B must still be immediately usable.
  assert(slots.take_latest() == b);
  assert(slots.begin_receive(replaced)==a && !replaced);
  slots.release(a);
  slots.release(b);
  assert(slots.take_latest() == -1);
}

static void test_incomplete_and_validation_ownership() {
  FrameSlots slots;
  bool replaced = false;
  int a = slots.begin_receive(replaced);
  int b = slots.begin_receive(replaced);
  assert(slots.begin_receive(replaced) == -1);
  slots.received(a);
  assert(slots.begin_validate() == a);
  assert(slots.begin_receive(replaced) == -1);  // Never reclaim validation data.
  slots.release(a);
  assert(slots.begin_receive(replaced) == a);
  slots.release(a);
  slots.release(b);
}

static void test_never_send_an_older_frame_after_latest() {
  FrameSlots slots;
  bool replaced = false;
  for (int n = 0; n < 2; ++n) {
    const int slot = slots.begin_receive(replaced);
    slots.received(slot);
    assert(slots.begin_validate() == slot);
    slots.publish(slot);
  }
  // Without an active USB consumer, keep accepting fresh data by replacing oldest.
  const int replace=slots.begin_receive(replaced);
  assert(replace>=0 && replaced);
  slots.received(replace); assert(slots.begin_validate()==replace); slots.publish(replace);
  const int latest = slots.take_latest();
  assert(latest >= 0);
  slots.release(latest);
  assert(slots.take_latest() == -1);  // No backwards motion after a slow USB interval.
}

static void test_clock_wrap_and_long_idle() {
  FrameClock clock(33333);
  assert(!clock.due(33332));
  assert(clock.due(33333));
  clock.sent(100);
  assert(clock.due(0x80010000));  // Reopen after >35 minutes, not a future deadline.
  clock.sent(0xfffffff0);
  assert(!clock.due(0x20));
  assert(clock.due(0x9000));
  clock.interval(200000);
  clock.sent(100);
  assert(!clock.due(200099) && clock.due(200100));
}

int main() {
  // Timing reports must retain a gap crossing report boundaries, handle timer
  // wrap, and exclude an idle/new-stream interval after reset.
  FrameGap gaps;
  gaps.observe(0);
  assert(gaps.take_max() == 0);
  gaps.observe(100000);
  assert(gaps.take_max() == 100000);
  gaps.observe(200000);
  gaps.observe(500000);
  assert(gaps.take_max() == 300000);
  assert(gaps.take_max() == 0);
  gaps.reset();
  gaps.observe(0xfffffff0u);
  assert(gaps.take_max() == 0);
  gaps.observe(0x20u);
  assert(gaps.take_max() == 48);
  test_crc_and_frame_validation();
  test_jpeg_bounds();
  test_slow_usb_ownership();
  test_incomplete_and_validation_ownership();
  test_never_send_an_older_frame_after_latest();
  test_clock_wrap_and_long_idle();
  std::puts("USB camera tests passed: CRC, malformed frames, JPEG bounds, slow USB ownership.");
}
