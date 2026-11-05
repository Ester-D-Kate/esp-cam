#include <assert.h>
#include <stdio.h>
#include "tusb.h"
#include "usb_cam/modes.h"

static unsigned le16(const uint8_t* p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t le32(const uint8_t* p) {
  return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void check_profile(UsbCamProfile profile) {
  usb_cam_usb_select_profile(profile);
  const unsigned expected = profile == USB_CAM_PROFILE_DISCOVERY ? 0 :
      profile == USB_CAM_PROFILE_OV3660 ? 10 : 9;
  const tusb_desc_device_t* device = (const tusb_desc_device_t*)tud_descriptor_device_cb();
  assert(device->idProduct == 0x4050 + (unsigned)profile);
  const uint8_t* config = tud_descriptor_configuration_cb(0);
  assert(config && config[0] == 9 && config[1] == TUSB_DESC_CONFIGURATION);
  assert(!tud_descriptor_configuration_cb(1));
  const size_t length = le16(config + 2);
  assert(length > 9 && length < 1024 && config[4] == (expected ? 4 : 2));
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
      static const unsigned sizes[][2] = {{640,480},{800,600},{1280,720},{320,240},
          {160,120},{480,320},{1024,768},{1280,1024},{1600,1200},{1920,1080}};
      assert(d[3] == video_frames && video_frames <= expected);
      assert(le16(d + 5) == sizes[video_frames - 1][0]);
      assert(le16(d + 7) == sizes[video_frames - 1][1]);
      assert(le32(d + 17) == 65536);
      // UXGA may send 65536 bytes per 666666 * 100 ns. Advertise the
      // corresponding maximum rounded up, rather than the old 720 KiB/s cap.
      assert(le32(d + 13) == (video_frames >= 9 ? 7864328u : 5898240u));
      assert(d[25] >= 2 && d[0] == 26 + d[25] * 4);
      uint32_t previous = 0;
      for (unsigned n = 0; n < d[25]; ++n) {
        const uint32_t interval = le32(d + 26 + 4 * n);
        assert(interval > previous && interval >= 166666 && interval <= 2000000);
        previous = interval;
      }
      assert(usb_cam_valid_interval(usb_cam_profile_mode(profile, d[3]), le32(d + 21)));
    } else if (d[1] == TUSB_DESC_ENDPOINT && d[2] == 0x81) {
      ++video_endpoints;
      assert((d[3] & 3) == TUSB_XFER_BULK && le16(d + 4) == 64 && d[6] == 0);
    }
    offset += d[0];
  }
  assert(offset == length && interfaces == (expected ? 4 : 2) && video_frames == expected && video_endpoints == (expected ? 1 : 0));
  for (unsigned i = 0; i <= 5; ++i) {
    const uint16_t* string = tud_descriptor_string_cb(i, 0x0409);
    assert(string && (string[0] >> 8) == TUSB_DESC_STRING && (string[0] & 255) <= 128);
  }
  assert(!tud_descriptor_string_cb(6, 0x0409));
}
int main(void) {
  assert(usb_cam_usb_profile() == USB_CAM_PROFILE_DISCOVERY);
  for (unsigned p = 0; p <= USB_CAM_PROFILE_OV3660; ++p) check_profile((UsbCamProfile)p);
  // Return to the smaller sensor and prove extended indices disappear.
  check_profile(USB_CAM_PROFILE_OV2640);
  puts("USB descriptors passed: discovery, both sensors, legacy fallback, indices and identities.");
}
