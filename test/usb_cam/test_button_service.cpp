#include <cassert>
#include <cstdio>
#include <cstring>
#include "button_hid.h"
#include "receiver.h"
#include "tusb.h"
using namespace usb_cam;
// TinyUSB supplies this weak no-op unless the application handles remount.
extern "C" __attribute__((weak)) void tud_mount_cb() {}
static ButtonBridge bridge;
uint64_t test_time_us=110000;
static bool ready=false,accept=false,resync_during_send=false;
static unsigned calls=0;
static uint8_t sent[16];
bool receiver_button_peek(ButtonReport& r){return bridge.peek(r);}
void receiver_button_commit(const ButtonReport& r){bridge.commit(r);}
ButtonReport receiver_button_snapshot(){return bridge.snapshot();}
void receiver_button_resync(){bridge.request_resync();}
void receiver_button_expire(uint32_t now){bridge.expire(now);}
bool tud_hid_ready(){return ready;}
bool tud_hid_report(uint8_t id,const void* data,uint16_t n){assert(id==1 && n==16);++calls;memcpy(sent,data,16);if(resync_during_send)bridge.request_resync();return accept;}
int main(){
 bridge.observe(true,{1,0,0,false},100);bridge.observe(true,{2,0,110,false},110);
 auto n=bridge.pending();button_hid_poll();assert(calls==0 && bridge.pending()==n);
 ready=true;button_hid_poll();assert(calls==1 && bridge.pending()==n);
 accept=true;button_hid_poll();assert(bridge.pending()==0 && sent[1]==(kButtonAvailable|kButtonSync));
 bridge.observe(true,{2,1,120,true},120);test_time_us=120000;
 uint8_t data[16];assert(tud_hid_get_report_cb(0,1,HID_REPORT_TYPE_INPUT,data,16)==16);
 assert(data[1]==(kButtonAvailable|kButtonPressed|kButtonSync));assert(bridge.pending()==1);
 assert(tud_hid_get_report_cb(1,1,HID_REPORT_TYPE_INPUT,data,16)==0);
 assert(tud_hid_get_report_cb(0,1,HID_REPORT_TYPE_INPUT,data,15)==0);
 const uint8_t command=1;auto rev=bridge.snapshot().revision;
 tud_hid_set_report_cb(0,1,HID_REPORT_TYPE_FEATURE,&command,1);assert(bridge.snapshot().revision==rev);
 tud_hid_set_report_cb(0,2,HID_REPORT_TYPE_OUTPUT,&command,1);assert(bridge.snapshot().revision==rev);
 tud_hid_set_report_cb(0,2,HID_REPORT_TYPE_FEATURE,&command,1);assert(bridge.ack().sequence==kButtonResync);
 bridge.observe(true,{3,0,125,true},125);resync_during_send=true;
 button_hid_poll();assert(bridge.pending()==1); // Concurrent reset cannot pop the replacement snapshot.
 resync_during_send=false;tud_umount_cb();assert(!(bridge.snapshot().flags & kButtonAvailable));
 // A USB bus reset does not call umount in pinned TinyUSB. SET_CONFIGURATION
 // calls mount before HID delivery resumes; old offline edges must be invalid.
 bridge=ButtonBridge{};
 bridge.observe(true,{10,0,130,false},130);bridge.observe(true,{11,0,140,false},140);
 bridge.observe(true,{11,1,150,true},150);
 tud_mount_cb();
 assert(!(bridge.snapshot().flags & kButtonAvailable));
 assert(bridge.ack().sequence==kButtonResync);
 puts("HID service passed: no reader/failed submission never waits, GET is state, resync races retain snapshot.");
}
