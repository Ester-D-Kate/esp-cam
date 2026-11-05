#include <Arduino.h>
#include <WiFi.h>
#include <esp_timer.h>
#include "spi_link.h"
#include "camera_control.h"

namespace {
CameraControl camera;
usb_cam::Header counters;
bool link_initialized = false;
int64_t next_capture = 0, last_exchange = 0, last_report = 0;
uint32_t sent = 0;
}
void setup() {
  // Disconnect programmer TX: GPIO3 is READY. GPIO2 is SPI MISO.
  Serial.begin(115200, SERIAL_8N1, -1, 1);
  delay(250);
  Serial.println("USB POV camera v2: Pico controls sensor; remove SD/servo/gyro wiring");
  WiFi.mode(WIFI_OFF); btStop();
  link_initialized = camera_link::begin();
  if (!link_initialized) Serial.println("ERROR: SPI initialization failed");
}
void loop() {
  if (!link_initialized) { delay(1000); return; }
  const int64_t now = esp_timer_get_time();
  if (now - last_report >= 1000000) {
    Serial.printf("sent=%lu sensor=0x%04x gen=%lu config_error=%lu capture_errors=%lu oversized=%lu command_errors=%lu\n",
        (unsigned long)sent, counters.sensor_pid, (unsigned long)counters.generation,
        (unsigned long)counters.config_error, (unsigned long)counters.capture_errors,
        (unsigned long)counters.oversized, (unsigned long)counters.command_errors);
    last_report = now;
  }
  if (!camera_link::ready()) { delay(1); return; }
  camera_fb_t* frame = nullptr;
  size_t length = 0;
  if (camera.can_capture() && now >= next_capture) {
    next_capture = now + camera.interval_us();
    frame = camera.capture(counters, length);
  } else if (now - last_exchange < 20000) {
    delay(1); return;
  }
  camera.describe(counters);
  counters.kind = frame ? usb_cam::PacketKind::Frame : usb_cam::PacketKind::Status;
  counters.payload_size = frame ? length : 0;
  counters.payload_crc = frame ? usb_cam::crc32(frame->buf, length) : 0;
  if (frame) counters.timestamp_us = uint32_t(uint64_t(frame->timestamp.tv_sec) * 1000000 + frame->timestamp.tv_usec);
  uint8_t header[usb_cam::kHeaderSize], response[usb_cam::kControlSize];
  usb_cam::encode_header(counters, header);
  const bool exchanged = camera_link::exchange(header, frame ? frame->buf : nullptr,
                                               frame ? length : 0, response);
  // Never retain a camera buffer while applying/reinitializing settings.
  if (frame) esp_camera_fb_return(frame);
  last_exchange = esp_timer_get_time();
  if (!exchanged) { ++counters.link_timeouts; return; }
  if (frame) ++sent;
  usb_cam::Config desired;
  if (usb_cam::decode_config(response, sizeof(response), desired)) camera.configure(desired);
  else ++counters.command_errors;
}
