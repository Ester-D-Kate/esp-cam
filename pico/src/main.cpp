#include <cstdio>
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "tusb.h"
#include "receiver.h"
#include "usb_cam/frame_clock.h"
#include "usb_cam/commands.h"

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
usb_cam::Config desired;
unsigned selected_mode = USB_CAM_DEFAULT_MODE;
uint32_t stale_frames = 0;
bool custom_quality = false;
bool report_requested = false;

void commands() {
  static usb_cam::LineReader reader;
  // Bounded work each USB iteration; a flooded CDC endpoint cannot starve video.
  uint8_t bytes[64];
  const uint32_t count = tud_cdc_read(bytes, sizeof(bytes));
  for (uint32_t i = 0; i < count; ++i) {
    if (!reader.push(char(bytes[i]))) continue;
    if (!reader.overflowed() && strcmp(reader.line(), "bootsel") == 0) {
      tud_disconnect();
      reset_usb_boot(0, 0);
    }
    const auto result = reader.overflowed() ? usb_cam::CommandResult::Invalid :
                        usb_cam::apply_command(reader.line(), desired);
    if (result == usb_cam::CommandResult::Changed) {
      if (strncmp(reader.line(), "set quality ", 12) == 0) custom_quality = true;
      if (strcmp(reader.line(), "defaults") == 0) custom_quality = false;
      receiver_configure(desired);
    }
    report_requested = true;
    const char* reply = result == usb_cam::CommandResult::Invalid ?
        "ERR invalid command or setting\r\n" : "OK desired state accepted; check applied_generation/config_error\r\n";
    if (tud_cdc_write_available() >= strlen(reply)) tud_cdc_write_str(reply);
  }
}

void diagnostics() {
  static uint32_t last_report = 0;
  static uint32_t previous_completed = 0;
  static uint32_t previous_received = 0;
  const uint32_t now = time_us_32();
  const uint32_t elapsed = now - last_report;
  if (elapsed < 1000000 && !report_requested) return;
  if (!elapsed) return;
  report_requested = false;
  const ReceiverStats rx = receiver_stats();
  const uint32_t usb_fps_x10 = uint32_t(uint64_t(completed - previous_completed) * 10000000 / elapsed);
  const uint32_t rx_fps_x10 = uint32_t(uint64_t(rx.frames - previous_received) * 10000000 / elapsed);
  previous_completed = completed;
  previous_received = rx.frames;
  last_report = now;
  if (!tud_cdc_connected()) return;
  char prefix[sizeof(rx.last_prefix) * 2 + 1];
  constexpr char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < sizeof(rx.last_prefix); ++i) {
    prefix[2 * i] = hex[rx.last_prefix[i] >> 4];
    prefix[2 * i + 1] = hex[rx.last_prefix[i] & 15];
  }
  prefix[sizeof(prefix) - 1] = '\0';
  char control_prefix[33];
  for (size_t i = 0; i < sizeof(rx.sender.control_prefix); ++i) {
    control_prefix[2 * i] = hex[rx.sender.control_prefix[i] >> 4];
    control_prefix[2 * i + 1] = hex[rx.sender.control_prefix[i] & 15];
  }
  control_prefix[32] = '\0';
  char line[1536];
  const int count = snprintf(line, sizeof(line),
      "{\"mode\":%u,\"width\":%u,\"height\":%u,\"target_fps\":%u,"
      "\"rx_fps_x10\":%lu,\"usb_fps_x10\":%lu,\"rx_frames\":%lu,\"usb_frames\":%lu,"
      "\"invalid\":%lu,\"spi_timeouts\":%lu,\"replaced\":%lu,\"seq_gaps\":%lu,"
      "\"sender_resets\":%lu,\"submit_errors\":%lu,\"cancelled\":%lu,\"usb_restarts\":%lu,"
      "\"sequence\":%lu,\"jpeg_bytes\":%lu,\"max_queue_us\":%lu,\"capture_errors\":%lu,"
      "\"oversized\":%lu,\"link_timeouts\":%lu,\"quality\":%u,\"clk_peri\":%lu,"
      "\"header_errors\":%lu,\"length_errors\":%lu,\"payload_errors\":%lu,\"overflows\":%lu,"
      "\"rx_bytes\":%lu,\"rx_prefix\":\"%s\","
      "\"desired_generation\":%lu,\"applied_generation\":%lu,\"config_error\":%lu,"
      "\"attempted_generation\":%lu,\"sensor_pid\":%u,\"command_errors\":%lu,"
      "\"stale_frames\":%lu,\"status_packets\":%lu,\"xclk_mhz\":%u,"
      "\"desired_token\":%lu,\"applied_token\":%lu,"
      "\"control_error\":%lu,\"control_crc_received\":%lu,\"control_crc_calculated\":%lu,"
      "\"control_prefix\":\"%s\",\"control_spi_hz\":%lu}\r\n",
      selected_mode, desired.width, desired.height, unsigned(10000000u / desired.interval_100ns),
      (unsigned long)rx_fps_x10, (unsigned long)usb_fps_x10,
      (unsigned long)rx.frames, (unsigned long)completed, (unsigned long)rx.invalid,
      (unsigned long)rx.timeouts, (unsigned long)rx.replaced, (unsigned long)rx.sequence_gaps,
      (unsigned long)rx.sender_resets, (unsigned long)submit_errors, (unsigned long)cancelled,
      (unsigned long)usb_restarts, (unsigned long)last_sequence, (unsigned long)last_bytes,
      (unsigned long)max_queue_us, (unsigned long)rx.sender.capture_errors,
      (unsigned long)rx.sender.oversized, (unsigned long)rx.sender.link_timeouts, rx.sender.quality,
      (unsigned long)rx.peripheral_hz, (unsigned long)rx.header_errors,
      (unsigned long)rx.length_errors, (unsigned long)rx.payload_errors,
      (unsigned long)rx.overflows, (unsigned long)rx.last_rx_bytes, prefix,
      (unsigned long)desired.generation, (unsigned long)rx.sender.generation,
      (unsigned long)rx.sender.config_error, (unsigned long)rx.sender.attempted_generation,
      rx.sender.sensor_pid, (unsigned long)rx.sender.command_errors,
      (unsigned long)stale_frames, (unsigned long)rx.status_packets, desired.xclk_mhz,
      (unsigned long)usb_cam::config_token(desired), (unsigned long)rx.sender.config_token,
      (unsigned long)rx.sender.control_error, (unsigned long)rx.sender.control_crc_received,
      (unsigned long)rx.sender.control_crc_calculated, control_prefix,
      (unsigned long)rx.sender.control_spi_hz);
  // A closed/slow serial monitor must never stall the UVC stream.
  if (count > 0 && size_t(count) < sizeof(line) && tud_cdc_write_available() >= uint32_t(count)) {
    tud_cdc_write(line, uint32_t(count));
    tud_cdc_write_flush();
  }

}
}  // namespace

extern "C" void tud_video_frame_xfer_complete_cb(uint_fast8_t control, uint_fast8_t stream) {
  (void)control; (void)stream;
  ++completed;
}

extern "C" int tud_video_commit_cb(uint_fast8_t control, uint_fast8_t stream,
                                   const video_probe_and_commit_control_t* parameters) {
  const UsbCamMode* mode = usb_cam_mode(parameters->bFrameIndex);
  if (control != 0 || stream != 0 || parameters->bFormatIndex != 1 ||
      !usb_cam_valid_interval(mode, parameters->dwFrameInterval)) return VIDEO_ERROR_OUT_OF_RANGE;
  usb_cam::Config next = usb_cam::mode_config(parameters->bFrameIndex, parameters->dwFrameInterval);
  next.generation = desired.generation;
  next.xclk_mhz = desired.xclk_mhz;
  memcpy(next.settings, desired.settings, sizeof(next.settings));
  if (custom_quality) next.quality = desired.quality;
  if (!usb_cam::config_equal(next, desired)) {
    next.generation++;
    desired = next;
    receiver_configure(desired);
  }
  selected_mode = parameters->bFrameIndex;
  return VIDEO_ERROR_NONE;
}

int main() {
  receiver_init();
  receiver_configure(desired);
  multicore_launch_core1(receiver_run);
  if (!tud_init(0)) panic("TinyUSB initialization failed");
  ReceivedFrame* sending = nullptr;
  uint32_t sent_at = 0;
  uint32_t completed_at_submit = 0;
  usb_cam::FrameClock frame_clock(desired.interval_100ns / 10);
  for (;;) {
    tud_task();
    commands();
    frame_clock.interval(desired.interval_100ns / 10);
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
      if (sending && !usb_cam::matches_config(sending->header, desired)) {
        receiver_release(sending); sending = nullptr; ++stale_frames;
      }
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
