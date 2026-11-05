// Compile the SDK's pinned, unmodified TinyUSB video driver in this translation
// unit so one accessor can report authoritative buffer ownership. Stop/restart
// and bus reset can release a frame without a frame-complete callback. A failed
// submission can also retain it. Polling only tud_video_n_streaming is unsafe.
// Keep this adapter isolated: a TinyUSB update must recheck these private fields.
#include "tusb.h"
#include "usb_cam/modes.h"
static bool usb_cam_control_xfer(uint8_t rhport, const tusb_control_request_t* request,
                                void* buffer, uint16_t length);
#define tud_control_xfer usb_cam_control_xfer
#define videod_control_xfer_cb usb_cam_sdk_video_control
#include "class/video/video_device.c"
#undef videod_control_xfer_cb
#undef tud_control_xfer

_Static_assert(TUSB_VERSION_NUMBER == 1800, "Review the ownership adapter when updating TinyUSB");

static bool camera_stream_control(const tusb_control_request_t* request) {
  const uint8_t selector = TU_U16_HIGH(request->wValue);
  return request->bmRequestType_bit.type == TUSB_REQ_TYPE_CLASS &&
      request->bmRequestType_bit.recipient == TUSB_REQ_RCPT_INTERFACE && request->wIndex == 1 &&
      (selector == VIDEO_VS_CTL_PROBE || selector == VIDEO_VS_CTL_COMMIT);
}

static bool normalize_probe(video_probe_and_commit_control_t* p, uint8_t request, bool strict) {
  if (!strict) {
    if (!p->bFormatIndex) p->bFormatIndex = 1;
    if (!p->bFrameIndex) p->bFrameIndex = request == VIDEO_REQUEST_GET_MAX ? USB_CAM_MODE_COUNT : USB_CAM_DEFAULT_MODE;
  }
  const UsbCamMode* mode = usb_cam_mode(p->bFrameIndex);
  if (p->bFormatIndex != 1 || !mode) return false;
  if (!usb_cam_valid_interval(mode, p->dwFrameInterval)) {
    if (strict) return false;
    p->dwFrameInterval = request == VIDEO_REQUEST_GET_MIN ? mode->intervals[0] :
        request == VIDEO_REQUEST_GET_MAX ? mode->intervals[mode->interval_count - 1] : mode->default_interval;
  }
  // 0.18 otherwise derives an uncompressed YUV size, and returns partial
  // defaults when more than one frame/interval is present. Bound MJPEG to RAM.
  p->dwMaxVideoFrameSize = USB_CAM_BUFFER_BYTES;
  p->dwMaxPayloadTransferSize = CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE;
  return true;
}

static bool usb_cam_control_xfer(uint8_t rhport, const tusb_control_request_t* request,
                                void* buffer, uint16_t length) {
  if (camera_stream_control(request) && request->bmRequestType_bit.direction == TUSB_DIR_IN &&
      length == sizeof(video_probe_and_commit_control_t) && request->bRequest != VIDEO_REQUEST_GET_RES) {
    if (!normalize_probe(buffer, request->bRequest, false)) return false;
  }
  return tud_control_xfer(rhport, request, buffer, length);
}

bool videod_control_xfer_cb(uint8_t rhport, uint8_t stage, const tusb_control_request_t* request) {
  const bool setting = camera_stream_control(request) && request->bRequest == VIDEO_REQUEST_SET_CUR &&
      stage == CONTROL_STAGE_DATA;
  videod_streaming_interface_t* stream = _get_instance_streaming(0, 0);
  if (setting) {
    if (!stream) return false;
    const bool commit = TU_U16_HIGH(request->wValue) == VIDEO_VS_CTL_COMMIT;
    if (!normalize_probe(&stream->probe_commit_payload, VIDEO_REQUEST_SET_CUR, commit)) return false;
    if (commit && stream->desc.ep[0]) {
      // Quiesce the old endpoint before the class clears its buffer pointer.
      // Our RP2040 adapter aborts DMA and clears busy/claimed state on reopen.
      const uint8_t* base = _videod_itf[stream->index_vc].beg;
      if (!usbd_edpt_open(rhport, (const tusb_desc_endpoint_t*)(base + stream->desc.ep[0]))) return false;
    }
  }
  const bool result = usb_cam_sdk_video_control(rhport, stage, request);
  if (result && setting) {
    stream->probe_commit_payload.dwMaxPayloadTransferSize = CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE;
    stream->max_payload_transfer_size = CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE;
  }
  return result;
}

bool usb_cam_video_owns_buffer(const void* buffer) {
  videod_streaming_interface_t* stream = _get_instance_streaming(0, 0);
  return stream && stream->bufsize && stream->buffer == buffer;
}
