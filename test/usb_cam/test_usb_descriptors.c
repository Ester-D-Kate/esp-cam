#include <assert.h>
#include <stdio.h>
#include "tusb.h"
#include "usb_cam/profile.h"

static unsigned le16(const uint8_t* p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t le32(const uint8_t* p) {
  return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int main(void) {
  const uint8_t* config = tud_descriptor_configuration_cb(0);
  assert(config && config[0] == 9 && config[1] == TUSB_DESC_CONFIGURATION);
  assert(!tud_descriptor_configuration_cb(1));
  const size_t length = le16(config + 2);
  assert(length > 9 && length < 512 && config[4] == 4);
  unsigned interfaces = 0;
  unsigned video_frames = 0;
  unsigned video_endpoints = 0;
  unsigned interface_class = 0;
  unsigned interface_subclass = 0;
  size_t offset = 0;
  while (offset < length) {
    const uint8_t* d = config + offset;
    assert(d[0] >= 2 && offset + d[0] <= length);
    if (d[1] == TUSB_DESC_INTERFACE) {
      assert(d[0] == 9);
      if (d[3] == 0) ++interfaces;
      interface_class = d[5];
      interface_subclass = d[6];
      if (interface_class == TUSB_CLASS_VIDEO && interface_subclass == VIDEO_SUBCLASS_STREAMING) {
        assert(d[3] == 0 && d[4] == 1);  // Bulk streaming has one endpoint in alt 0.
      }
    } else if (d[1] == TUSB_DESC_CS_INTERFACE && interface_class == TUSB_CLASS_VIDEO &&
               interface_subclass == VIDEO_SUBCLASS_STREAMING && d[2] == VIDEO_CS_ITF_VS_FRAME_MJPEG) {
      ++video_frames;
      assert(d[0] == 38 && d[3] == 1);
      assert(le16(d + 5) == USB_CAM_WIDTH && le16(d + 7) == USB_CAM_HEIGHT);
      assert(le32(d + 17) == USB_CAM_MAX_JPEG);
      assert(le32(d + 21) == USB_CAM_INTERVAL_100NS);
      assert(d[25] == 0 && le32(d + 26) == le32(d + 30));
    } else if (d[1] == TUSB_DESC_ENDPOINT && d[2] == 0x81) {
      ++video_endpoints;
      assert((d[3] & 3) == TUSB_XFER_BULK && le16(d + 4) == 64 && d[6] == 0);
    }
    offset += d[0];
  }
  assert(offset == length && interfaces == 4 && video_frames == 1 && video_endpoints == 1);
  for (unsigned i = 0; i <= 5; ++i) {
    const uint16_t* string = tud_descriptor_string_cb(i, 0x0409);
    assert(string && (string[0] >> 8) == TUSB_DESC_STRING && (string[0] & 255) <= 128);
  }
  assert(!tud_descriptor_string_cb(6, 0x0409));
  printf("USB descriptor tests passed for profile %u (%ux%u at %u target FPS).\n",
         USB_CAM_PROFILE, USB_CAM_WIDTH, USB_CAM_HEIGHT, USB_CAM_FPS);
}
