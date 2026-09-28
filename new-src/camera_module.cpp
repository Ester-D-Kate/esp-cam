#include "app_shared.h"

bool initCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
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
  config.frame_size = FRAMESIZE_SVGA;
  config.jpeg_quality = 10;
  config.fb_count = psramFound() ? 2 : 1;
  config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    cameraReady = false;
    return false;
  }

  sensor_t* sensor = esp_camera_sensor_get();
  if (sensor) {
    sensor->set_framesize(sensor, FRAMESIZE_SVGA);
    sensor->set_quality(sensor, 10);
  }

  cameraReady = true;
  Serial.println("Camera initialized");
  return true;
}

void handleStream() {
  if (!cameraReady) {
    server.send(503, "text/plain", "Camera not initialized");
    return;
  }

  if (!streamAvailable()) {
    if (runtimeMode == RuntimeMode::Recording) {
      server.send(409, "text/plain", "Recording in progress");
    } else {
      server.send(503, "text/plain", "Live stream is currently unavailable");
    }
    return;
  }

  WiFiClient client = server.client();
  client.setNoDelay(true);

  static const char header[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
    "Cache-Control: no-cache, no-store, must-revalidate\r\n"
    "Pragma: no-cache\r\n"
    "Connection: close\r\n\r\n";
  client.write(reinterpret_cast<const uint8_t*>(header), sizeof(header) - 1);

  streamClientActive = true;
  streamStopRequested = false;
  unsigned long lastFrameAt = 0;

  while (client.connected()) {
    processRuntimeDuringStream();
    if (streamStopRequested || !streamAvailable()) {
      break;
    }

    unsigned long now = millis();
    if (now - lastFrameAt < kRecordingFrameIntervalMs) {
      delay(1);
      continue;
    }

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed during streaming");
      break;
    }

    if (fb->format != PIXFORMAT_JPEG) {
      esp_camera_fb_return(fb);
      break;
    }

    char partBuf[96];
    size_t headerLen = snprintf(
      partBuf,
      sizeof(partBuf),
      "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
      static_cast<unsigned int>(fb->len)
    );

    client.write(reinterpret_cast<const uint8_t*>(partBuf), headerLen);
    client.write(fb->buf, fb->len);
    client.write(reinterpret_cast<const uint8_t*>("\r\n"), 2);

    esp_camera_fb_return(fb);
    lastFrameAt = now;
  }

  streamClientActive = false;
  streamStopRequested = false;
  client.stop();
}
