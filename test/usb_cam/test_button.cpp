#include <array>
#include <cassert>
#include <cstdio>
#include "usb_cam/button.h"
#include "usb_cam/protocol.h"
using namespace usb_cam;

static void debounce_edges_and_hold() {
  ButtonSource s; s.begin(42);
  assert(!s.snapshot().pressed && s.snapshot().sequence == 0);
  s.sample(true,100); s.sample(false,120); s.sample(true,130);
  s.sample(true,179); assert(s.snapshot().sequence==0);
  s.sample(true,180);
  auto e=s.snapshot(); assert(e.pressed && e.sequence==1 && e.timestamp_ms==180 && e.boot_id==42);
  s.sample(true,5000); assert(s.snapshot().sequence==1);
  assert(!s.acknowledge({43,1}) && !s.acknowledge({42,2}));
  s.sample(false,5010); s.sample(false,5059);
  assert(s.snapshot().sequence==1); // Old press retained until acknowledged.
  s.sample(false,5060);
  assert(s.acknowledge({42,1}));
  e=s.snapshot(); assert(!e.pressed && e.sequence==2 && e.timestamp_ms==5060);
  assert(!s.acknowledge({42,1}));
  assert(s.acknowledge({42,2}));
  assert(s.snapshot().sequence==2 && !s.snapshot().pressed);
  s.sample(true,6000); s.sample(true,6050);
  assert(s.resynchronize(42,43,6100));
  assert(s.pending()==0 && s.snapshot().pressed && s.snapshot().sequence==0);
  s.sample(true,6200); assert(s.snapshot().sequence==0);
  assert(!s.resynchronize(42,44,6300));
  s.sample(false,6300); s.sample(false,6350);
  assert(!s.snapshot().pressed && s.snapshot().sequence==1);
}
static void wrap_overflow_and_boot() {
  ButtonSource s; s.begin(7);
  s.sample(true,0xfffffff0u); s.sample(true,33); assert(s.snapshot().sequence==0);
  s.sample(true,34); assert(s.snapshot().sequence==1);
  assert(button_next_sequence(0x7fffffffu)==0);
  s.begin(8); assert(s.snapshot().boot_id==8 && s.snapshot().sequence==0);
  uint32_t t=0;
  for(unsigned i=1;i<=65;++i) {
    bool pressed=i&1; s.sample(pressed,t); s.sample(pressed,t+50); t+=100;
  }
  assert(s.lost()>0 && s.pending()<=32);
  assert(s.snapshot().sequence==65 && s.snapshot().pressed);
  assert(!s.acknowledge({7,65}));
}
static void wire_extension_and_camera_identity() {
  Config c=profile_defaults(USB_CAM_PROFILE_OV3660_FULL), decoded;
  std::array<uint8_t,kControlSize> bytes{};
  encode_config(c,bytes.data()); auto token=config_token(c);
  assert(valid_button_ack_bytes(bytes.data()));
  encode_button_ack({17,123},bytes.data());
  assert(decode_config(bytes.data(),bytes.size(),decoded));
  assert(config_equal(c,decoded) && config_token(decoded)==token);
  encode_button_ack({17,kButtonResync},bytes.data());
  assert(decode_config(bytes.data(),bytes.size(),decoded) && config_token(decoded)==token);
  encode_button_ack({17,123},bytes.data());
  auto ack=decode_button_ack(bytes.data());assert(ack.boot_id==17 && ack.sequence==123);
  bytes[106]^=1; assert(!decode_config(bytes.data(),bytes.size(),decoded));
  encode_config(c,bytes.data()); bytes[102]=1; put32(bytes.data()+124,crc32(bytes.data(),124));
  assert(!decode_config(bytes.data(),bytes.size(),decoded));
  encode_config(c,bytes.data()); encode_button_ack({17,123},bytes.data());
  bytes[114]=1;put32(bytes.data()+124,crc32(bytes.data(),124));
  assert(!decode_config(bytes.data(),bytes.size(),decoded));
  Header h, out;
  for(unsigned i=0;i<16;++i) h.control_prefix[i]=uint8_t(i+1);
  encode_header(h,bytes.data());assert(decode_header(bytes.data(),bytes.size(),out));
  assert(out.control_prefix[15]==16 && out.button.boot_id==0);
  h.capabilities|=kButtonCapability;h.button={42,12,345,true};
  encode_header(h,bytes.data());assert(decode_header(bytes.data(),bytes.size(),out));
  assert(out.button.boot_id==42 && out.button.sequence==12 && out.button.timestamp_ms==345 && out.button.pressed);
  assert(out.control_prefix[7]==8 && out.control_prefix[8]==0);
  for(unsigned i=0;i<128;++i){bytes[i]^=1;assert(!decode_header(bytes.data(),bytes.size(),out));bytes[i]^=1;}
  h.capabilities=0;encode_header(h,bytes.data());bytes[120]=1;put32(bytes.data()+124,crc32(bytes.data(),124));
  assert(!decode_header(bytes.data(),bytes.size(),out));
  h.capabilities=kButtonCapability;h.button.boot_id=0;encode_header(h,bytes.data());
  assert(!decode_header(bytes.data(),bytes.size(),out));
}
int main(){debounce_edges_and_hold();wrap_overflow_and_boot();wire_extension_and_camera_identity();puts("Button debounce, retained edges, overflow, wire CRC and camera-token invariance passed.");}
