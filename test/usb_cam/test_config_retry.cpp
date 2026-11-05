#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "usb_cam/config_retry.h"
using namespace usb_cam;
static Header failed(const Config& c, uint32_t error = kConfigInit) {
  Header h;
  h.config_error = error;
  h.attempted_generation = c.generation;
  h.attempted_token = config_token(c);
  return h;
}
int main() {
  Config desired;
  ConfigRetry retry;
  retry.new_request(10);
  Header h = failed(desired);
  assert(!retry.poll(h, 10, desired, 100)); // Retained status is not fresh.
  assert(!retry.poll(h, 11, desired, 100));
  assert(!retry.poll(h, 12, desired, 250099));
  assert(!retry.poll(h, 12, desired, 500100)); // Link stopped: don't retry.
  const Config original = desired;
  assert(retry.poll(h, 13, desired, 500101));
  assert(desired.generation == original.generation + 1 && retry.attempts() == 1);
  Config same = desired; same.generation = original.generation;
  assert(config_equal(same, original)); // Only the request ID changes.
  assert(!retry.poll(h, 14, desired, 800000)); // Ignore previous attempt's error.
  h = failed(desired);
  assert(!retry.poll(h, 15, desired, 800000));
  assert(retry.poll(h, 16, desired, 1050000));
  assert(retry.attempts() == 2);
  h = failed(desired);
  assert(!retry.poll(h, 17, desired, 2000000));
  assert(!retry.poll(h, 18, desired, 3000000)); // Budget survives automatic generations.

  retry.new_request(18);
  for (uint32_t error : {1u, 3u, 4u, 5u, 32u}) {
    h = failed(desired, error);
    assert(!retry.poll(h, error + 100, desired, error * 1000000));
  }
  h = failed(desired); h.attempted_token ^= 1;
  assert(!retry.poll(h, 200, desired, 100));
  h = failed(desired);
  assert(!retry.poll(h, 201, desired, 100));
  h.config_error = 0;
  h.kind = PacketKind::Frame;
  assert(!retry.poll(h, 201, desired, 250100)); // Frames don't increment status count.
  h = failed(desired);
  assert(!retry.poll(h, 203, desired, 250101));
  assert(!retry.poll(h, 204, desired, 500100));
  assert(retry.poll(h, 205, desired, 500101));

  retry.new_request(205);
  h = failed(desired);
  assert(!retry.poll(h, 206, desired, 0xffff0000));
  assert(retry.poll(h, 207, desired, uint32_t(0xffff0000u + 250000u)));
  // A new user request gets its own budget and waits for fresh status.
  desired.width = 800; desired.height = 600; desired.generation++;
  retry.new_request(207);
  assert(retry.attempts() == 0);
  assert(!retry.poll(h, 208, desired, 100));
  h = failed(desired);
  assert(!retry.poll(h, 209, desired, 100));
  assert(retry.poll(h, 210, desired, 250100));
  // Capture timeout, failed sensor reset and clock I/O need the same bounded
  // recovery as initialization; unsupported controls must not be retried.
  for (uint32_t error : {6u, 7u, 8u}) {
    retry.new_request(0);
    h = failed(desired, error);
    assert(!retry.poll(h, 1, desired, 100));
    assert(retry.poll(h, 2, desired, 250100));
    h = failed(desired, error);
    assert(!retry.poll(h, 3, desired, 500000));
    assert(retry.poll(h, 4, desired, 750000));
    h = failed(desired, error);
    assert(!retry.poll(h, 5, desired, 1000000));
    assert(!retry.poll(h, 6, desired, 2000000));
  }
  puts("Configuration retry passed: freshness, bounded attempts, identity, cancellation and clock wrap.");
}
