#include "button_input.h"
#include <Arduino.h>
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

namespace button_input {
namespace {
constexpr int kPin = 12;
portMUX_TYPE mutex = portMUX_INITIALIZER_UNLOCKED;
usb_cam::ButtonSource source;
esp_timer_handle_t timer = nullptr;
bool active = false;
uint32_t new_session(uint32_t old = 0) {
  uint32_t next;
  do { next = esp_random(); } while (!next || next == old);
  return next;
}
void poll(void*) {
  const bool pressed = digitalRead(kPin) == LOW;
  const uint32_t now = uint32_t(uint64_t(esp_timer_get_time()) / 1000);
  portENTER_CRITICAL(&mutex);
  source.sample(pressed, now);
  portEXIT_CRITICAL(&mutex);
}
}
bool begin() {
  if (active) return true;
  pinMode(kPin, INPUT_PULLUP); // Runtime only: preserve GPIO12's reset strap.
  source.begin(new_session());
  esp_timer_create_args_t args{};
  args.callback = poll; args.dispatch_method = ESP_TIMER_TASK;
  args.name = "camera_button"; args.skip_unhandled_events = true;
  esp_err_t result = esp_timer_create(&args, &timer);
  if (result == ESP_OK) {
    result = esp_timer_start_periodic(timer, 5000);
    if (result != ESP_OK) { esp_timer_delete(timer); timer = nullptr; }
  }
  active = result == ESP_OK;
  if (!active) Serial.printf("Button timer unavailable: %ld; camera continues\n", (long)result);
  return active;
}
void write_header(usb_cam::Header& header) {
  header.capabilities &= ~usb_cam::kButtonCapability;
  if (!active) return;
  portENTER_CRITICAL(&mutex);
  header.button = source.snapshot();
  portEXIT_CRITICAL(&mutex);
  header.capabilities |= usb_cam::kButtonCapability;
}
void acknowledge(usb_cam::ButtonAck ack) {
  if (!active || !ack.boot_id) return;
  const uint32_t session = ack.sequence == usb_cam::kButtonResync ? new_session(ack.boot_id) : 0;
  const uint32_t now = uint32_t(uint64_t(esp_timer_get_time()) / 1000);
  portENTER_CRITICAL(&mutex);
  if (session) source.resynchronize(ack.boot_id, session, now);
  else source.acknowledge(ack);
  portEXIT_CRITICAL(&mutex);
}
}
