#pragma once
#include <cstdint>
#include "usb_cam/protocol.h"

struct ReceivedFrame {
  alignas(4) uint8_t packet[usb_cam::kPacketCapacity + 1];  // Extra-byte overrun detection.
  usb_cam::Header header;
  uint32_t received_us;
  uint32_t length;
};

struct ReceiverStats {
  uint32_t frames = 0;
  uint32_t invalid = 0;
  uint32_t header_errors = 0;
  uint32_t length_errors = 0;
  uint32_t payload_errors = 0;
  uint32_t overflows = 0;
  uint32_t timeouts = 0;
  uint32_t replaced = 0;
  uint32_t sequence_gaps = 0;
  uint32_t sender_resets = 0;
  uint32_t last_sequence = 0;
  uint32_t max_bytes = 0;
  uint32_t peripheral_hz = 0;
  uint32_t last_rx_bytes = 0;
  uint8_t last_prefix[16] = {};
};

void receiver_init();
void receiver_run();  // Core 1 only; never returns.
ReceivedFrame* receiver_take_latest();
void receiver_release(ReceivedFrame* frame);
ReceiverStats receiver_stats();
