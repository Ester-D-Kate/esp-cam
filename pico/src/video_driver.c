// Compile the SDK's pinned, unmodified TinyUSB video driver in this translation
// unit so one accessor can report authoritative buffer ownership. Stop/restart
// and bus reset can release a frame without a frame-complete callback. A failed
// submission can also retain it. Polling only tud_video_n_streaming is unsafe.
// Keep this adapter isolated: a TinyUSB update must recheck these private fields.
#include "class/video/video_device.c"

_Static_assert(TUSB_VERSION_NUMBER == 1800, "Review the ownership adapter when updating TinyUSB");

bool usb_cam_video_owns_buffer(const void* buffer) {
  videod_streaming_interface_t* stream = _get_instance_streaming(0, 0);
  return stream && stream->bufsize && stream->buffer == buffer;
}
