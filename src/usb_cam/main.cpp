#include <Arduino.h>
#include <WiFi.h>
#include <esp_camera.h>
#include <esp_timer.h>
#include "spi_link.h"
#include "usb_cam/protocol.h"

#define CAMERA_MODEL_AI_THINKER
#include "../camera_pins.h"

namespace {
usb_cam::Header counters;
bool initialized = false;
uint32_t sent = 0;
int64_t next_capture = 0;
int64_t last_log = 0;
int64_t last_wait_log = 0;

framesize_t capture_size() {
#if USB_CAM_PROFILE == 1
  return FRAMESIZE_VGA;
#elif USB_CAM_PROFILE == 2
  return FRAMESIZE_SVGA;
#elif USB_CAM_PROFILE == 3
  return FRAMESIZE_HD;
#else
  return FRAMESIZE_QVGA;
#endif
}

bool begin_camera() {
  if (!psramFound()) {
    Serial.println("ERROR: PSRAM is required for USB camera capture");
    return false;
  }
  camera_config_t config{};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  // Reserve generous driver buffers before selecting output geometry. The SDK
  // estimates capacity from pixels/5, which can under-allocate detailed JPEGs.
  config.frame_size = FRAMESIZE_UXGA;
  config.jpeg_quality = usb_cam::kProfile.quality;
  config.fb_count = 2;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;
  const esp_err_t result = esp_camera_init(&config);
  if (result != ESP_OK) {
    Serial.printf("ERROR: camera initialization: 0x%x\n", result);
    return false;
  }
  sensor_t* sensor = esp_camera_sensor_get();
  if (!sensor || (sensor->id.PID != OV2640_PID && sensor->id.PID != OV3660_PID) ||
      !sensor->set_framesize || !sensor->set_quality ||
      sensor->set_framesize(sensor, capture_size()) != 0 ||
      sensor->set_quality(sensor, counters.quality) != 0) {
    Serial.println("ERROR: sensor/profile unsupported; expected OV2640 or OV3660");
    esp_camera_deinit();
    return false;
  }
  Serial.printf("Sensor PID=0x%04x, profile=%u, %ux%u, target=%u FPS, JPEG ceiling=%lu\n",
                sensor->id.PID, USB_CAM_PROFILE, usb_cam::kProfile.width,
                usb_cam::kProfile.height, usb_cam::kProfile.fps,
                static_cast<unsigned long>(usb_cam::kProfile.max_jpeg));
  // Discard frames from the initial buffer-allocation geometry.
  for (int i = 0; i < 3; ++i) {
    camera_fb_t* frame = esp_camera_fb_get();
    if (frame) esp_camera_fb_return(frame);
  }
  return true;
}

void report() {
  const int64_t now = esp_timer_get_time();
  if (now - last_log < 1000000) return;
  Serial.printf("sent=%lu seq=%lu capture_errors=%lu oversized=%lu link_timeouts=%lu quality=%u\n",
                static_cast<unsigned long>(sent), static_cast<unsigned long>(counters.sequence),
                static_cast<unsigned long>(counters.capture_errors),
                static_cast<unsigned long>(counters.oversized),
                static_cast<unsigned long>(counters.link_timeouts), counters.quality);
  last_log = now;
}
}  // namespace

void setup() {
  // The programmer's TX wire must be disconnected: GPIO3 is now READY input.
  Serial.begin(115200, SERIAL_8N1, -1, 1);
  delay(250);
  Serial.println("USB POV camera: remove SD card and detach servo/gyro wiring");
  WiFi.mode(WIFI_OFF);
  btStop();
  initialized = camera_link::begin() && begin_camera();
  if (!initialized) Serial.println("Startup stopped. Fix the reported error and reset.");
}

void loop() {
  if (!initialized) { delay(1000); return; }
  report();
  if (!camera_link::ready()) {
    const int64_t now = esp_timer_get_time();
    if (now - last_wait_log >= 2000000) {
      Serial.println("Waiting for Pico READY; check power, wiring and receiver firmware");
      last_wait_log = now;
    }
    delay(1);
    return;
  }
  const int64_t now = esp_timer_get_time();
  if (now < next_capture) {
    const int64_t remaining = next_capture - now;
    if (remaining >= 2000) delay(1);
    else delayMicroseconds(uint32_t(remaining));
    return;
  }
  next_capture = now + 1000000 / usb_cam::kProfile.fps;
  camera_fb_t* frame = esp_camera_fb_get();
  ++counters.sequence;
  if (!frame) { ++counters.capture_errors; return; }
  size_t length = frame->len;
  if (frame->format != PIXFORMAT_JPEG ||
      !usb_cam::validate_jpeg(frame->buf, length, usb_cam::kProfile.width, usb_cam::kProfile.height)) {
    ++counters.capture_errors;
  } else if (length > usb_cam::kProfile.max_jpeg) {
    ++counters.oversized;
    // Increase compression only when the current scene exceeds the wire budget.
    // Never silently resize, repeat a frame, or claim this frame was delivered.
    if (counters.quality < 50) {
      sensor_t* sensor = esp_camera_sensor_get();
      const uint8_t quality = counters.quality + 2;
      if (sensor->set_quality(sensor, quality) == 0) counters.quality = quality;
    }
  } else {
    counters.timestamp_us = uint32_t(uint64_t(frame->timestamp.tv_sec) * 1000000 + frame->timestamp.tv_usec);
    counters.payload_size = length;
    counters.payload_crc = usb_cam::crc32(frame->buf, length);
    alignas(4) uint8_t header[usb_cam::kHeaderSize];
    usb_cam::encode_header(counters, header);
    if (camera_link::send(header, frame->buf, length)) ++sent;
    else ++counters.link_timeouts;
  }
  esp_camera_fb_return(frame);
}
