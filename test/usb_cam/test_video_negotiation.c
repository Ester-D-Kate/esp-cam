#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tusb.h"
#include "device/usbd_pvt.h"
#include "usb_cam/modes.h"
static video_probe_and_commit_control_t reply;
static void* outbound;
static unsigned transfer_count, complete_count;
static uint16_t transfer_lengths[40];
static bool claim_ok = true, transfer_ok = true;
extern bool usb_cam_video_owns_buffer(const void* buffer);
bool tud_control_xfer(uint8_t port, const tusb_control_request_t* request, void* buffer, uint16_t length) {
  (void)port;
  if (request->bmRequestType_bit.direction == TUSB_DIR_IN && length == sizeof(reply)) memcpy(&reply, buffer, length);
  else outbound = buffer;
  return true;
}
bool tud_control_status(uint8_t port, const tusb_control_request_t* request) { (void)port; (void)request; return true; }
bool usbd_edpt_open(uint8_t port, const tusb_desc_endpoint_t* endpoint) { (void)port; (void)endpoint; return true; }
void usbd_edpt_close(uint8_t port, uint8_t endpoint) { (void)port; (void)endpoint; }
bool usbd_edpt_claim(uint8_t port, uint8_t endpoint) { (void)port; (void)endpoint; return claim_ok; }
bool usbd_edpt_xfer(uint8_t port, uint8_t endpoint, uint8_t* buffer, uint16_t size) {
  (void)port; (void)endpoint;
  assert(size == 0 || buffer);
  assert(transfer_count < 40);
  transfer_lengths[transfer_count++] = size;
  return transfer_ok;
}
bool usbd_edpt_release(uint8_t port, uint8_t endpoint) { (void)port; (void)endpoint; return true; }
int tud_video_commit_cb(uint_fast8_t control, uint_fast8_t stream, const video_probe_and_commit_control_t* p) {
  (void)control; (void)stream;
  return p->bFormatIndex == 1 && usb_cam_valid_interval(usb_cam_mode(p->bFrameIndex), p->dwFrameInterval) ?
      VIDEO_ERROR_NONE : VIDEO_ERROR_OUT_OF_RANGE;
}
void tud_video_frame_xfer_complete_cb(uint_fast8_t control, uint_fast8_t stream) {
  (void)control; (void)stream; ++complete_count;
}
int tud_video_power_mode_cb(uint_fast8_t control, uint8_t mode) { (void)control; (void)mode; return VIDEO_ERROR_NONE; }

static void recommit(void) {
  tusb_control_request_t request = {.bmRequestType = 0x21, .bRequest = VIDEO_REQUEST_SET_CUR,
      .wValue = VIDEO_VS_CTL_COMMIT << 8, .wIndex = 1, .wLength = sizeof(reply)};
  assert(videod_control_xfer_cb(0, CONTROL_STAGE_SETUP, &request));
  video_probe_and_commit_control_t params = {0};
  params.bFormatIndex = 1; params.bFrameIndex = USB_CAM_DEFAULT_MODE;
  params.dwFrameInterval = usb_cam_mode(USB_CAM_DEFAULT_MODE)->default_interval;
  memcpy(outbound, &params, sizeof(params));
  assert(videod_control_xfer_cb(0, CONTROL_STAGE_DATA, &request));
}

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
  // A short bulk payload ending on a full USB packet requires a terminating
  // ZLP. Keep frame ownership until it completes, or host URBs can merge frames.
  static uint8_t frame[2108];
  transfer_count = complete_count = 0;
  assert(tud_video_n_frame_xfer(0, 0, frame, 62));
  assert(transfer_count == 1 && transfer_lengths[0] == 64);
  assert(videod_xfer_cb(0, 0x81, XFER_RESULT_SUCCESS, 64));
  assert(transfer_count == 2 && transfer_lengths[1] == 0);
  assert(complete_count == 0 && usb_cam_video_owns_buffer(frame));
  assert(videod_xfer_cb(0, 0x81, XFER_RESULT_SUCCESS, 0));
  assert(complete_count == 1 && !usb_cam_video_owns_buffer(frame));

  transfer_count = complete_count = 0;
  assert(tud_video_n_frame_xfer(0, 0, frame, 61));
  assert(transfer_lengths[0] == 63);
  assert(videod_xfer_cb(0, 0x81, XFER_RESULT_SUCCESS, 63));
  assert(complete_count == 1 && transfer_count == 1);

  transfer_count = complete_count = 0;
  assert(tud_video_n_frame_xfer(0, 0, frame, 2046));
  assert(transfer_lengths[0] == 2048);
  assert(videod_xfer_cb(0, 0x81, XFER_RESULT_SUCCESS, 2048));
  assert(complete_count == 1 && transfer_count == 1);

  transfer_count = complete_count = 0;
  assert(tud_video_n_frame_xfer(0, 0, frame, sizeof(frame)));
  assert(transfer_lengths[0] == 2048);
  assert(videod_xfer_cb(0, 0x81, XFER_RESULT_SUCCESS, 2048));
  assert(transfer_count == 2 && transfer_lengths[1] == 64);
  assert(videod_xfer_cb(0, 0x81, XFER_RESULT_SUCCESS, 64));
  assert(transfer_count == 3 && transfer_lengths[2] == 0 && complete_count == 0);
  assert(videod_xfer_cb(0, 0x81, XFER_RESULT_SUCCESS, 0));
  assert(complete_count == 1 && !usb_cam_video_owns_buffer(frame));

  // Failed termination must never release the buffer or signal success.
  transfer_count = complete_count = 0;
  assert(tud_video_n_frame_xfer(0, 0, frame, 62));
  claim_ok = false;
  assert(!videod_xfer_cb(0, 0x81, XFER_RESULT_SUCCESS, 64));
  assert(transfer_count == 1 && complete_count == 0 && usb_cam_video_owns_buffer(frame));
  claim_ok = true;
  recommit();
  assert(!usb_cam_video_owns_buffer(frame));

  transfer_count = complete_count = 0;
  assert(tud_video_n_frame_xfer(0, 0, frame, 62));
  transfer_ok = false;
  assert(!videod_xfer_cb(0, 0x81, XFER_RESULT_SUCCESS, 64));
  assert(transfer_count == 2 && complete_count == 0 && usb_cam_video_owns_buffer(frame));
  transfer_ok = true;
  recommit();

  transfer_count = complete_count = 0;
  assert(tud_video_n_frame_xfer(0, 0, frame, 61));
  assert(!videod_xfer_cb(0, 0x81, XFER_RESULT_FAILED, 63));
  assert(transfer_count == 1 && complete_count == 0 && usb_cam_video_owns_buffer(frame));
  recommit();

  // A failed ZLP callback must also retain ownership until recovery.
  transfer_count = complete_count = 0;
  assert(tud_video_n_frame_xfer(0, 0, frame, 62));
  assert(videod_xfer_cb(0, 0x81, XFER_RESULT_SUCCESS, 64));
  assert(transfer_count == 2 && usb_cam_video_owns_buffer(frame));
  assert(!videod_xfer_cb(0, 0x81, XFER_RESULT_FAILED, 0));
  assert(complete_count == 0 && usb_cam_video_owns_buffer(frame));
  recommit();
  assert(complete_count == 0 && !usb_cam_video_owns_buffer(frame));

  // A new COMMIT cancels a still-pending ZLP; the next frame transfers normally.
  transfer_count = complete_count = 0;
  assert(tud_video_n_frame_xfer(0, 0, frame, 62));
  assert(videod_xfer_cb(0, 0x81, XFER_RESULT_SUCCESS, 64));
  assert(transfer_count == 2 && usb_cam_video_owns_buffer(frame));
  recommit();
  assert(complete_count == 0 && !usb_cam_video_owns_buffer(frame));
  assert(tud_video_n_frame_xfer(0, 0, frame, 61));
  assert(videod_xfer_cb(0, 0x81, XFER_RESULT_SUCCESS, 63));
  assert(complete_count == 1 && !usb_cam_video_owns_buffer(frame));

  request.bmRequestType = 0x21; request.bRequest = VIDEO_REQUEST_SET_CUR;
  assert(videod_control_xfer_cb(0, CONTROL_STAGE_SETUP, &request));
  memset(outbound, 0, sizeof(reply));
  ((video_probe_and_commit_control_t*)outbound)->bFormatIndex = 1;
  ((video_probe_and_commit_control_t*)outbound)->bFrameIndex = 250;
  assert(!videod_control_xfer_cb(0, CONTROL_STAGE_DATA, &request));
  puts("Pinned TinyUSB passed: negotiation, bulk termination, failed transfers and cancellation.");
}
