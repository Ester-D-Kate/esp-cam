#include <cstdio>
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include "receiver.h"
#include "usb_cam/frame_clock.h"

extern "C" bool usb_cam_video_owns_buffer(const void* buffer);
extern "C" bool usb_cam_usb_recovery_required(void);

namespace {
uint32_t completed = 0;
uint32_t submit_errors = 0;
uint32_t cancelled = 0;
uint32_t usb_restarts = 0;
uint32_t last_sequence = 0;
uint32_t last_bytes = 0;
uint32_t max_queue_us = 0;
usb_cam::Header sender;

void diagnostics() {
  static uint32_t last_report = 0;
  static uint32_t previous_completed = 0;
  static uint32_t previous_received = 0;
  const uint32_t now = time_us_32();
  const uint32_t elapsed = now - last_report;
  if (elapsed < 1000000) return;
  const ReceiverStats rx = receiver_stats();
  const uint32_t usb_fps_x10 = uint32_t(uint64_t(completed - previous_completed) * 10000000 / elapsed);
  const uint32_t rx_fps_x10 = uint32_t(uint64_t(rx.frames - previous_received) * 10000000 / elapsed);
  previous_completed = completed;
  previous_received = rx.frames;
  last_report = now;
  if (!tud_cdc_connected()) return;
  char line[768];
  const int count = snprintf(line, sizeof(line),
      "{\"profile\":%u,\"width\":%u,\"height\":%u,\"target_fps\":%u,"
      "\"rx_fps_x10\":%lu,\"usb_fps_x10\":%lu,\"rx_frames\":%lu,\"usb_frames\":%lu,"
      "\"invalid\":%lu,\"spi_timeouts\":%lu,\"replaced\":%lu,\"seq_gaps\":%lu,"
      "\"sender_resets\":%lu,\"submit_errors\":%lu,\"cancelled\":%lu,\"usb_restarts\":%lu,"
      "\"sequence\":%lu,\"jpeg_bytes\":%lu,\"max_queue_us\":%lu,\"capture_errors\":%lu,"
      "\"oversized\":%lu,\"link_timeouts\":%lu,\"quality\":%u,\"clk_peri\":%lu}\r\n",
      USB_CAM_PROFILE, usb_cam::kProfile.width, usb_cam::kProfile.height, usb_cam::kProfile.fps,
      (unsigned long)rx_fps_x10, (unsigned long)usb_fps_x10,
      (unsigned long)rx.frames, (unsigned long)completed, (unsigned long)rx.invalid,
      (unsigned long)rx.timeouts, (unsigned long)rx.replaced, (unsigned long)rx.sequence_gaps,
      (unsigned long)rx.sender_resets, (unsigned long)submit_errors, (unsigned long)cancelled,
      (unsigned long)usb_restarts, (unsigned long)last_sequence, (unsigned long)last_bytes,
      (unsigned long)max_queue_us, (unsigned long)sender.capture_errors,
      (unsigned long)sender.oversized, (unsigned long)sender.link_timeouts, sender.quality,
      (unsigned long)rx.peripheral_hz);
  // A closed/slow serial monitor must never stall the UVC stream.
  if (count > 0 && size_t(count) < sizeof(line) && tud_cdc_write_available() >= uint32_t(count)) {
    tud_cdc_write(line, uint32_t(count));
    tud_cdc_write_flush();
  }
  uint8_t discard[64];
  while (tud_cdc_available()) tud_cdc_read(discard, sizeof(discard));
}
}  // namespace

extern "C" void tud_video_frame_xfer_complete_cb(uint_fast8_t control, uint_fast8_t stream) {
  (void)control; (void)stream;
  ++completed;
}

extern "C" int tud_video_commit_cb(uint_fast8_t control, uint_fast8_t stream,
                                   const video_probe_and_commit_control_t* parameters) {
  if (control != 0 || stream != 0 || parameters->bFormatIndex != 1 || parameters->bFrameIndex != 1 ||
      parameters->dwFrameInterval != USB_CAM_INTERVAL_100NS) return VIDEO_ERROR_OUT_OF_RANGE;
  return VIDEO_ERROR_NONE;
}

int main() {
  receiver_init();
  multicore_launch_core1(receiver_run);
  if (!tud_init(0)) panic("TinyUSB initialization failed");
  ReceivedFrame* sending = nullptr;
  uint32_t sent_at = 0;
  uint32_t completed_at_submit = 0;
  usb_cam::FrameClock frame_clock(1000000 / usb_cam::kProfile.fps);
  for (;;) {
    tud_task();
    const uint32_t now = time_us_32();
    if (usb_cam_usb_recovery_required() || (sending && uint32_t(now - sent_at) > 2000000)) {
      // A wedged transfer must not consume a slot forever. Disconnect and fully
      // deinitialize the controller before releasing memory; no speculative free.
      tud_disconnect();
      if (!tud_deinit(0)) panic("USB recovery failed");
      if (sending) {
        receiver_release(sending);
        ++cancelled;
      }
      sending = nullptr;
      ++usb_restarts;
      sleep_ms(25);
      if (!tud_init(0)) panic("USB recovery initialization failed");
      frame_clock.sent(time_us_32());
      continue;
    }
    if (sending && !usb_cam_video_owns_buffer(sending->packet + usb_cam::kHeaderSize)) {
      if (completed == completed_at_submit) ++cancelled;
      receiver_release(sending);
      sending = nullptr;
    }
    if (!sending && tud_ready() && tud_video_n_streaming(0, 0) && frame_clock.due(now)) {
      sending = receiver_take_latest();
      if (sending) {
        const uint32_t queue_us = now - sending->received_us;
        if (queue_us > max_queue_us) max_queue_us = queue_us;
        sender = sending->header;
        last_sequence = sender.sequence;
        last_bytes = sender.payload_size;
        sent_at = now;
        completed_at_submit = completed;
        const bool submitted = tud_video_n_frame_xfer(0, 0,
            sending->packet + usb_cam::kHeaderSize, sender.payload_size);
        if (!submitted) ++submit_errors;
        // Even false may retain the pointer. The ownership accessor decides when
        // it is safe to recycle this slot on the next loop iteration.
        frame_clock.sent(now);
      }
    }
    diagnostics();
    tight_loop_contents();
  }
}
