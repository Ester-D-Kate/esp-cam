#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tusb.h"
#include "device/usbd_pvt.h"
#include "usb_cam/modes.h"
static video_probe_and_commit_control_t reply;
static void* outbound;
bool tud_control_xfer(uint8_t port, const tusb_control_request_t* request, void* buffer, uint16_t length) {
  (void)port;
  if (request->bmRequestType_bit.direction == TUSB_DIR_IN && length == sizeof(reply)) memcpy(&reply, buffer, length);
  else outbound = buffer;
  return true;
}
bool tud_control_status(uint8_t port, const tusb_control_request_t* request) { (void)port; (void)request; return true; }
bool usbd_edpt_open(uint8_t port, const tusb_desc_endpoint_t* endpoint) { (void)port; (void)endpoint; return true; }
void usbd_edpt_close(uint8_t port, uint8_t endpoint) { (void)port; (void)endpoint; }
bool usbd_edpt_claim(uint8_t port, uint8_t endpoint) { (void)port; (void)endpoint; return true; }
bool usbd_edpt_xfer(uint8_t port, uint8_t endpoint, uint8_t* buffer, uint16_t size) {
  (void)port; (void)endpoint; (void)buffer; (void)size; return true;
}
bool usbd_edpt_release(uint8_t port, uint8_t endpoint) { (void)port; (void)endpoint; return true; }
int tud_video_commit_cb(uint_fast8_t control, uint_fast8_t stream, const video_probe_and_commit_control_t* p) {
  (void)control; (void)stream;
  return p->bFormatIndex == 1 && usb_cam_valid_interval(usb_cam_mode(p->bFrameIndex), p->dwFrameInterval) ?
      VIDEO_ERROR_NONE : VIDEO_ERROR_OUT_OF_RANGE;
}
void tud_video_frame_xfer_complete_cb(uint_fast8_t control, uint_fast8_t stream) { (void)control; (void)stream; }
int tud_video_power_mode_cb(uint_fast8_t control, uint8_t mode) { (void)control; (void)mode; return VIDEO_ERROR_NONE; }

int main(void) {
  const uint8_t* config = tud_descriptor_configuration_cb(0);
  videod_init();
  assert(videod_open(0, (const tusb_desc_interface_t*)(config + 17), 1024) > 0);
  tusb_control_request_t request = {.bmRequestType = 0xa1, .bRequest = VIDEO_REQUEST_GET_DEF,
      .wValue = VIDEO_VS_CTL_PROBE << 8, .wIndex = 1, .wLength = sizeof(reply)};
  assert(videod_control_xfer_cb(0, CONTROL_STAGE_SETUP, &request));
  assert(reply.bFrameIndex == 1 && reply.dwFrameInterval == 333333);
  assert(reply.dwMaxVideoFrameSize == 65536 && reply.dwMaxPayloadTransferSize == 2048);
  for (unsigned i = 1; i <= 9; ++i) {
    const UsbCamMode* mode = usb_cam_mode(i);
    for (unsigned n = 0; n < mode->interval_count; ++n) {
      request.bmRequestType = 0x21; request.bRequest = VIDEO_REQUEST_SET_CUR;
      request.wValue = VIDEO_VS_CTL_COMMIT << 8;
      assert(videod_control_xfer_cb(0, CONTROL_STAGE_SETUP, &request));
      video_probe_and_commit_control_t params = {0};
      params.bFormatIndex = 1; params.bFrameIndex = i; params.dwFrameInterval = mode->intervals[n];
      memcpy(outbound, &params, sizeof(params));
      assert(videod_control_xfer_cb(0, CONTROL_STAGE_DATA, &request));
      request.bmRequestType = 0xa1; request.bRequest = VIDEO_REQUEST_GET_CUR;
      assert(videod_control_xfer_cb(0, CONTROL_STAGE_SETUP, &request));
      assert(reply.bFrameIndex == i && reply.dwFrameInterval == params.dwFrameInterval);
      assert(reply.dwMaxVideoFrameSize == 65536 && reply.dwMaxPayloadTransferSize == 2048);
    }
  }
  request.bmRequestType = 0x21; request.bRequest = VIDEO_REQUEST_SET_CUR;
  assert(videod_control_xfer_cb(0, CONTROL_STAGE_SETUP, &request));
  memset(outbound, 0, sizeof(reply));
  ((video_probe_and_commit_control_t*)outbound)->bFormatIndex = 1;
  ((video_probe_and_commit_control_t*)outbound)->bFrameIndex = 250;
  assert(!videod_control_xfer_cb(0, CONTROL_STAGE_DATA, &request));
  puts("Pinned TinyUSB negotiation passed: defaults, every mode/rate and invalid commits.");
}
