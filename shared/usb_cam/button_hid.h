#pragma once
#include "button.h"
namespace usb_cam {
constexpr uint8_t kButtonPressed = 1, kButtonAvailable = 2, kButtonSync = 4, kButtonLoss = 8;
constexpr size_t kButtonReportBytes = 16;
struct ButtonReport {
  ButtonEvent event;
  uint8_t flags = kButtonSync;
  uint16_t revision = 0;
};
inline void encode_button_report(const ButtonReport& report, uint8_t* out) {
  out[0] = 1; out[1] = report.flags;
  put32(out+2,report.event.sequence); put32(out+6,report.event.timestamp_ms);
  put32(out+10,report.event.boot_id); put16(out+14,report.revision);
}

// Called under one short lock by the SPI producer and USB consumer. No I/O here.
class ButtonBridge {
 public:
  void observe(bool available, const ButtonEvent& event, uint32_t now_ms) {
    received_at_ = now_ms; expired_ = false; have_packet_ = true;
    if (!available) {
      if (session_ || waiting_session_) { session_ = waiting_session_ = 0; request_resync(); }
      return;
    }
    if (waiting_) {
      if (!waiting_session_) waiting_session_ = event.boot_id;
      if (event.boot_id == waiting_session_) return;
      waiting_ = false; waiting_session_ = 0; session_ = event.boot_id;
      latest_ = event; queue_.clear();
      emit(event, uint8_t(kButtonAvailable | kButtonSync | (loss_pending_ ? kButtonLoss : 0)));
      loss_pending_ = false;
      return;
    }
    if (event.boot_id != session_) {
      session_ = event.boot_id; request_resync(); return;
    }
    const uint32_t delta = (event.sequence - latest_.sequence) & kButtonSequenceMask;
    if (!delta) return;
    if (delta != 1) { request_resync(true); return; }
    if (queue_.size() == 32) { request_resync(true); return; }
    latest_ = event; emit(event, kButtonAvailable);
  }
  void request_resync(bool loss = false) {
    waiting_ = true; loss_pending_ = loss_pending_ || loss;
    if (!waiting_session_) waiting_session_ = session_;
    queue_.clear();
    ButtonEvent unavailable = latest_; unavailable.pressed = false;
    emit(unavailable, uint8_t(kButtonSync | (loss_pending_ ? kButtonLoss : 0)));
  }
  void expire(uint32_t now_ms) {
    if (have_packet_ && !expired_ && uint32_t(now_ms - received_at_) >= 2000) {
      expired_ = true; request_resync();
    }
  }
  ButtonAck ack() const {
    if (waiting_) return {waiting_session_, kButtonResync};
    return {session_, latest_.sequence};
  }
  bool peek(ButtonReport& report) const {
    const auto* front = queue_.peek(); if (!front) return false;
    report = *front; return true;
  }
  bool commit(const ButtonReport& report) {
    const auto* front = queue_.peek();
    if (!front || front->revision != report.revision || front->event.boot_id != report.event.boot_id ||
        front->event.sequence != report.event.sequence) return false;
    queue_.pop(); return true;
  }
  ButtonReport snapshot() const { return report_; }
  size_t pending() const { return queue_.size(); }
 private:
  void emit(const ButtonEvent& event, uint8_t flags) {
    report_.event = event;
    report_.flags = flags | (event.pressed && (flags & kButtonAvailable) ? kButtonPressed : 0);
    ++report_.revision;
    queue_.push(report_); // Caller has made capacity; never waits for the reader.
  }
  ButtonQueue<ButtonReport,32> queue_;
  ButtonEvent latest_;
  ButtonReport report_;
  uint32_t session_ = 0, waiting_session_ = 0, received_at_ = 0;
  bool waiting_ = true, have_packet_ = false, expired_ = false, loss_pending_ = false;
};
} // namespace usb_cam
