#include <string.h>
#include "tusb.h"
#include "pico/unique_id.h"
#include "usb_cam/profile.h"

// Descriptor topology follows TinyUSB's MIT-licensed video_capture example.
// Macros are provided by TinyUSB; frame limits are our actual JPEG buffer sizes.
enum { VIDEO_CONTROL, VIDEO_STREAM, CDC_CONTROL, CDC_DATA, INTERFACE_COUNT };
#define VIDEO_ENDPOINT 0x81
#define VIDEO_LENGTH (TUD_VIDEO_DESC_IAD_LEN + TUD_VIDEO_DESC_STD_VC_LEN + \
    TUD_VIDEO_DESC_CS_VC_LEN + 1 + TUD_VIDEO_DESC_CAMERA_TERM_LEN + \
    TUD_VIDEO_DESC_OUTPUT_TERM_LEN + TUD_VIDEO_DESC_STD_VS_LEN + \
    TUD_VIDEO_DESC_CS_VS_IN_LEN + 1 + TUD_VIDEO_DESC_CS_VS_FMT_MJPEG_LEN + \
    TUD_VIDEO_DESC_CS_VS_FRM_MJPEG_CONT_LEN + TUD_VIDEO_DESC_CS_VS_COLOR_MATCHING_LEN + \
    7)
#define CONFIG_LENGTH (TUD_CONFIG_DESC_LEN + VIDEO_LENGTH + TUD_CDC_DESC_LEN)

static const tusb_desc_device_t device = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200, .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON, .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0xcafe, .idProduct = 0x4020 + USB_CAM_PROFILE,
    .bcdDevice = 0x0100, .iManufacturer = 1, .iProduct = 2,
    .iSerialNumber = 3, .bNumConfigurations = 1,
};

static const uint8_t configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, INTERFACE_COUNT, 0, CONFIG_LENGTH, 0, 100),
    TUD_VIDEO_DESC_IAD(VIDEO_CONTROL, 2, 4),
    TUD_VIDEO_DESC_STD_VC(VIDEO_CONTROL, 0, 4),
    TUD_VIDEO_DESC_CS_VC(0x0150,
        TUD_VIDEO_DESC_CAMERA_TERM_LEN + TUD_VIDEO_DESC_OUTPUT_TERM_LEN,
        27000000, VIDEO_STREAM),
    TUD_VIDEO_DESC_CAMERA_TERM(1, 0, 0, 0, 0, 0, 0),
    TUD_VIDEO_DESC_OUTPUT_TERM(2, VIDEO_TT_STREAMING, 0, 1, 0),
    TUD_VIDEO_DESC_STD_VS(VIDEO_STREAM, 0, 1, 4),
    TUD_VIDEO_DESC_CS_VS_INPUT(1,
        TUD_VIDEO_DESC_CS_VS_FMT_MJPEG_LEN + TUD_VIDEO_DESC_CS_VS_FRM_MJPEG_CONT_LEN +
        TUD_VIDEO_DESC_CS_VS_COLOR_MATCHING_LEN,
        VIDEO_ENDPOINT, 0, 2, 0, 0, 0, 0),
    TUD_VIDEO_DESC_CS_VS_FMT_MJPEG(1, 1, 0, 1, 0, 0, 0, 0),
    TUD_VIDEO_DESC_CS_VS_FRM_MJPEG_CONT(1, 0, USB_CAM_WIDTH, USB_CAM_HEIGHT,
        8 * USB_CAM_FPS, USB_CAM_MAX_JPEG * 8 * USB_CAM_FPS, USB_CAM_MAX_JPEG,
        USB_CAM_INTERVAL_100NS, USB_CAM_INTERVAL_100NS,
        USB_CAM_INTERVAL_100NS, USB_CAM_INTERVAL_100NS),
    TUD_VIDEO_DESC_CS_VS_COLOR_MATCHING(0, 0, 0),
    TUD_VIDEO_DESC_EP_BULK(VIDEO_ENDPOINT, 64, 0),
    TUD_CDC_DESCRIPTOR(CDC_CONTROL, 5, 0x82, 8, 0x03, 0x83, 64),
};

_Static_assert(sizeof(configuration) == CONFIG_LENGTH, "USB configuration length mismatch");

const uint8_t* tud_descriptor_device_cb(void) { return (const uint8_t*)&device; }
const uint8_t* tud_descriptor_configuration_cb(uint8_t index) {
  return index == 0 ? configuration : NULL;
}

const uint16_t* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  static uint16_t descriptor[64];
  static char serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
  static const char* const strings[] = {
      NULL, "Kate", "ESP32 POV Camera", serial, "MJPEG Video", "Camera Diagnostics"};
  size_t count;
  if (index == 0) {
    descriptor[1] = 0x0409;
    count = 1;
  } else {
    if (index >= sizeof(strings) / sizeof(strings[0])) return NULL;
    if (index == 3) pico_get_unique_board_id_string(serial, sizeof(serial));
    const char* string = strings[index];
    count = strlen(string);
    if (count > 63) count = 63;
    for (size_t i = 0; i < count; ++i) descriptor[i + 1] = (uint8_t)string[i];
  }
  descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * count + 2));
  return descriptor;
}
