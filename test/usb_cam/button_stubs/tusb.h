#pragma once
#include <cstdint>
enum hid_report_type_t { HID_REPORT_TYPE_INPUT=1,HID_REPORT_TYPE_OUTPUT=2,HID_REPORT_TYPE_FEATURE=3 };
bool tud_hid_ready();
bool tud_hid_report(uint8_t,const void*,uint16_t);
extern "C" uint16_t tud_hid_get_report_cb(uint8_t,uint8_t,hid_report_type_t,uint8_t*,uint16_t);
extern "C" void tud_hid_set_report_cb(uint8_t,uint8_t,hid_report_type_t,const uint8_t*,uint16_t);
extern "C" void tud_umount_cb();
extern "C" void tud_mount_cb();
