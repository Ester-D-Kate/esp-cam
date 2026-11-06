#include "camera_control.h"
#include "sensor_settings.h"
#include <Arduino.h>
#define CAMERA_MODEL_AI_THINKER
#include "../camera_pins.h"

void CameraControl::fail(uint32_t error) {
  dirty_ = true;
  session_.fail(error);
  Serial.printf("Camera error=%lu generation=%lu\n", (unsigned long)error,
      (unsigned long)session_.desired().generation);
}

bool CameraControl::initialize(const usb_cam::Config& desired) {
  if (!psramFound()) { fail(usb_cam::kConfigInit); return false; }
  camera_config_t config{};
  config.ledc_channel = LEDC_CHANNEL_0; config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = desired.xclk_mhz * 1000000;
  config.pixel_format = PIXFORMAT_JPEG;
  // The SDK clamps QXGA to the detected sensor maximum (UXGA on OV2640)
  // before cam_config allocates buffers. Keep that allocation across switches.
  config.frame_size = FRAMESIZE_QXGA;
  config.jpeg_quality = desired.quality;
  config.fb_count = 2; config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;
  ++camera_starts_;
  const esp_err_t result = esp_camera_init(&config);
  if (result != ESP_OK) {
    Serial.printf("Camera init result=%ld\n", (long)result);
    fail(usb_cam::kConfigInit); return false;
  }
  running_ = true;
  dirty_ = false;
  clock_known_ = true;
  xclk_mhz_ = desired.xclk_mhz;
  xclk_hz_ = ledc_get_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
  return true;
}

void CameraControl::configure(const usb_cam::Config& desired) {
  if (!session_.request(desired)) return;
  // The caller has returned its framebuffer. Allocate the camera once and
  // keep SCCB/I2S running across requests, as CameraWebServer does.
  const bool fresh = !running_;
  if (fresh && !initialize(desired)) return;
  sensor_t* sensor = esp_camera_sensor_get();
  sensor_pid_ = sensor ? sensor->id.PID : 0;
  if (!sensor || (sensor_pid_ != OV2640_PID && sensor_pid_ != OV3660_PID)) {
    fail(usb_cam::kConfigSensor); return;
  }
  const uint32_t validation = usb_cam::validate_sensor_target(sensor, desired);
  if (validation) { fail(validation); return; }
  const bool clock_changed = !clock_known_ || xclk_mhz_ != desired.xclk_mhz;
  if (clock_changed) {
    // set_xclk updates its cache even on failure; track success independently.
    clock_known_ = false;
    xclk_hz_ = 0;
    if (!sensor->set_xclk || sensor->set_xclk(sensor, LEDC_TIMER_0, desired.xclk_mhz)) {
      fail(usb_cam::kConfigClock); return;
    }
    xclk_mhz_ = desired.xclk_mhz;
    clock_known_ = true;
    xclk_hz_ = ledc_get_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
  }
  const bool settings_changed = memcmp(programmed_.settings, desired.settings,
                                       sizeof(desired.settings)) != 0;
  const bool reset = !fresh && (dirty_ || settings_changed ||
      (programmed_.readout.enabled && !desired.readout.enabled));
  if (reset) {
    ++sensor_resets_;
    const uint32_t error = usb_cam::reset_sensor_defaults(sensor);
    if (error) { fail(error); return; }
  }
  const bool full = fresh || !configured_ || reset;
  usb_cam::SensorUpdate update;
  update.geometry = full || clock_changed || !usb_cam::pll_equal(programmed_.pll, desired.pll) ||
                    !usb_cam::readout_equal(programmed_.readout, desired.readout) || programmed_.width != desired.width ||
                    programmed_.height != desired.height;
  update.quality = update.geometry || programmed_.quality != desired.quality ||
                   programmed_.max_jpeg != desired.max_jpeg;
  update.controls = update.geometry;
  const uint32_t error = usb_cam::apply_sensor_config(sensor, desired, update);
  if (error) { fail(error); return; }
  programmed_ = desired;
  configured_ = true; dirty_ = false;
  if (update.quality) quality_ = desired.quality;
  if (update.geometry || update.quality || update.controls) discard_ = 3;
  bad_geometry_ = 0;
  Serial.printf("Config generation=%lu sensor=0x%04x %ux%u interval=%lu xclk=%u MHz\n",
      (unsigned long)desired.generation, sensor_pid_, desired.width, desired.height,
      (unsigned long)desired.interval_100ns, desired.xclk_mhz);
}

camera_fb_t* CameraControl::capture(usb_cam::Header& counters, size_t& length) {
  if (!can_capture()) return nullptr;
  camera_fb_t* frame = esp_camera_fb_get();
  ++counters.sequence;
  if (!frame) {
    ++counters.capture_errors;
    // fb_get has already waited four seconds. Stop blocking subsequent control
    // exchanges and let the Pico's bounded recovery issue a fresh request.
    fail(usb_cam::kConfigFrame); return nullptr;
  }
  if (discard_) { --discard_; esp_camera_fb_return(frame); return nullptr; }
  const usb_cam::Config& config = session_.desired();
  length = frame->len;
  if (frame->format != PIXFORMAT_JPEG ||
      !usb_cam::validate_jpeg(frame->buf, length, config.width, config.height)) {
    ++counters.capture_errors;
    if (++bad_geometry_ >= 8) fail(usb_cam::kConfigFrame);
    esp_camera_fb_return(frame); return nullptr;
  }
  bad_geometry_ = 0;
  // Confirm only a real, correctly sized JPEG. Oversize is a transport limit,
  // reported separately, and must not block receiving the next command.
  session_.confirm(config.width, config.height);
  if (length > config.max_jpeg) {
    ++counters.oversized;
    if (quality_ < 50) {
      const uint8_t quality = quality_ < 48 ? quality_ + 2 : 50;
      sensor_t* sensor = esp_camera_sensor_get();
      if (!sensor || !sensor->set_quality || sensor->set_quality(sensor, quality))
        fail(usb_cam::kConfigQuality);
      else quality_ = quality;
    }
    esp_camera_fb_return(frame); return nullptr;
  }
  return frame;
}

void CameraControl::describe(usb_cam::Header& h) const {
  const auto& c = session_.desired();
  h.width = c.width; h.height = c.height; h.interval_100ns = c.interval_100ns;
  h.generation = session_.applied_generation(); h.config_token = session_.applied_token();
  h.attempted_generation = c.generation; h.attempted_token = usb_cam::config_token(c);
  h.config_error = session_.error(); h.quality = quality_; h.sensor_pid = sensor_pid_;
  h.capabilities = usb_cam::kSensorProfilesCapability | usb_cam::kFullModesCapability;
  h.camera_starts = camera_starts_; h.sensor_resets = sensor_resets_; h.xclk_hz = xclk_hz_;
}
