#include "button_hid.h"
#include "receiver.h"
#include "pico/stdlib.h"
#include "tusb.h"

void button_hid_reset() { receiver_button_resync(); }
void button_hid_poll() {
  const uint32_t now = uint32_t(to_ms_since_boot(get_absolute_time()));
  receiver_button_expire(now);
  if (!tud_hid_ready()) return;
  usb_cam::ButtonReport report;
  if (!receiver_button_peek(report)) return;
  uint8_t bytes[usb_cam::kButtonReportBytes];
  usb_cam::encode_button_report(report,bytes);
  // TinyUSB copies into its own endpoint buffer. Never wait for a host reader.
  if (tud_hid_report(1,bytes,sizeof(bytes))) receiver_button_commit(report);
}
extern "C" uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
    hid_report_type_t type, uint8_t* buffer, uint16_t length) {
  if (instance || report_id!=1 || type!=HID_REPORT_TYPE_INPUT || length<usb_cam::kButtonReportBytes) return 0;
  auto report=receiver_button_snapshot();
  report.flags |= usb_cam::kButtonSync; // GET_REPORT is state, never a click.
  usb_cam::encode_button_report(report,buffer);
  return usb_cam::kButtonReportBytes;
}
extern "C" void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
    hid_report_type_t type, uint8_t const* buffer, uint16_t length) {
  if (!instance && report_id==2 && type==HID_REPORT_TYPE_FEATURE && length==1 && buffer[0]==1)
    receiver_button_resync(); // Fresh snapshot handshake; no keyboard or camera action.
}
extern "C" void tud_umount_cb(void) { button_hid_reset(); }
// TinyUSB bus reset skips umount. Mount runs in tud_task before input resumes,
// so every SET_CONFIGURATION also invalidates offline edges without ISR locks.
extern "C" void tud_mount_cb(void) { button_hid_reset(); }
