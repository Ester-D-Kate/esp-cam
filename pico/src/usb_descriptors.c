#include <string.h>
#include "tusb.h"
#include "pico/unique_id.h"
#include "usb_cam/modes.h"

// Descriptor topology follows TinyUSB's MIT-licensed video_capture example.
// Macros are provided by TinyUSB; frame limits are our actual JPEG buffer sizes.
enum { VIDEO_CONTROL, VIDEO_STREAM, CDC_CONTROL, CDC_DATA, INTERFACE_COUNT };
#define VIDEO_ENDPOINT 0x81
#define FRAME_LENGTH(id, w, h, q, fps, ...) + TUD_VIDEO_DESC_CS_VS_FRM_MJPEG_DISC_LEN + 4 * TU_ARGS_NUM(__VA_ARGS__)
#define FRAMES_LENGTH(MODES) (0 MODES(FRAME_LENGTH))
// TinyUSB 0.18's discrete MJPEG macro copies its interval arguments as bytes.
// Encode the uint32 intervals explicitly while counting intervals, not bytes.
#define LE_INTERVALS_2(a, b) U32_TO_U8S_LE(a), U32_TO_U8S_LE(b)
#define LE_INTERVALS_3(a, b, c) U32_TO_U8S_LE(a), U32_TO_U8S_LE(b), U32_TO_U8S_LE(c)
#define LE_INTERVALS_6(a, b, c, d, e, f) LE_INTERVALS_3(a, b, c), LE_INTERVALS_3(d, e, f)
#define LE_INTERVALS_7(a, ...) U32_TO_U8S_LE(a), LE_INTERVALS_6(__VA_ARGS__)
#define JOIN_INNER(a, b) a##b
#define JOIN(a, b) JOIN_INNER(a, b)
#define LE_INTERVALS(...) JOIN(LE_INTERVALS_, TU_ARGS_NUM(__VA_ARGS__))(__VA_ARGS__)
#define VIDEO_LENGTH(MODES) (TUD_VIDEO_DESC_IAD_LEN + TUD_VIDEO_DESC_STD_VC_LEN + \
    TUD_VIDEO_DESC_CS_VC_LEN + 1 + TUD_VIDEO_DESC_CAMERA_TERM_LEN + \
    TUD_VIDEO_DESC_OUTPUT_TERM_LEN + TUD_VIDEO_DESC_STD_VS_LEN + \
    TUD_VIDEO_DESC_CS_VS_IN_LEN + 1 + TUD_VIDEO_DESC_CS_VS_FMT_MJPEG_LEN + \
    FRAMES_LENGTH(MODES) + TUD_VIDEO_DESC_CS_VS_COLOR_MATCHING_LEN + \
    7)
#define CONFIG_LENGTH(MODES) (TUD_CONFIG_DESC_LEN + VIDEO_LENGTH(MODES) + TUD_CDC_DESC_LEN)

#define DEVICE_DESCRIPTOR(pid) { \
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE, \
    .bcdUSB = 0x0200, .bDeviceClass = TUSB_CLASS_MISC, \
    .bDeviceSubClass = MISC_SUBCLASS_COMMON, .bDeviceProtocol = MISC_PROTOCOL_IAD, \
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE, \
    .idVendor = 0xcafe, .idProduct = pid, \
    .bcdDevice = 0x0300 + USB_CAM_ENABLE_60FPS, .iManufacturer = 1, .iProduct = 2, \
    .iSerialNumber = 3, .bNumConfigurations = 1 }
static const tusb_desc_device_t devices[] = {
    DEVICE_DESCRIPTOR(0x4050), DEVICE_DESCRIPTOR(0x4051),
    DEVICE_DESCRIPTOR(0x4052), DEVICE_DESCRIPTOR(0x4053)};
#define FIRST_INTERVAL(a, ...) a
#define FRAME_DESCRIPTOR(id, w, h, q, fps, ...) \
    26 + 4 * TU_ARGS_NUM(__VA_ARGS__), TUSB_DESC_CS_INTERFACE, VIDEO_CS_ITF_VS_FRAME_MJPEG, \
    id, 0, U16_TO_U8S_LE(w), U16_TO_U8S_LE(h), U32_TO_U8S_LE(8 * 5), \
    U32_TO_U8S_LE(USB_CAM_MAX_BITRATE_INTERVAL(w, h, FIRST_INTERVAL(__VA_ARGS__))), U32_TO_U8S_LE(USB_CAM_BUFFER_BYTES), \
    U32_TO_U8S_LE(USB_CAM_INTERVAL(fps)), TU_ARGS_NUM(__VA_ARGS__), LE_INTERVALS(__VA_ARGS__),
#define CONFIGURATION(MODES, COUNT) \
    TUD_CONFIG_DESCRIPTOR(1, INTERFACE_COUNT, 0, CONFIG_LENGTH(MODES), 0, 100), \
    TUD_VIDEO_DESC_IAD(VIDEO_CONTROL, 2, 4), \
    TUD_VIDEO_DESC_STD_VC(VIDEO_CONTROL, 0, 4), \
    TUD_VIDEO_DESC_CS_VC(0x0150, \
        TUD_VIDEO_DESC_CAMERA_TERM_LEN + TUD_VIDEO_DESC_OUTPUT_TERM_LEN, \
        27000000, VIDEO_STREAM), \
    TUD_VIDEO_DESC_CAMERA_TERM(1, 0, 0, 0, 0, 0, 0), \
    TUD_VIDEO_DESC_OUTPUT_TERM(2, VIDEO_TT_STREAMING, 0, 1, 0), \
    TUD_VIDEO_DESC_STD_VS(VIDEO_STREAM, 0, 1, 4), \
    TUD_VIDEO_DESC_CS_VS_INPUT(1, \
        TUD_VIDEO_DESC_CS_VS_FMT_MJPEG_LEN + FRAMES_LENGTH(MODES) + \
        TUD_VIDEO_DESC_CS_VS_COLOR_MATCHING_LEN, \
        VIDEO_ENDPOINT, 0, 2, 0, 0, 0, 0), \
    TUD_VIDEO_DESC_CS_VS_FMT_MJPEG(1, COUNT, 0, USB_CAM_DEFAULT_MODE, 0, 0, 0, 0), \
    MODES(FRAME_DESCRIPTOR) \
    TUD_VIDEO_DESC_CS_VS_COLOR_MATCHING(0, 0, 0), \
    TUD_VIDEO_DESC_EP_BULK(VIDEO_ENDPOINT, 64, 0), \
    TUD_CDC_DESCRIPTOR(CDC_CONTROL, 5, 0x82, 8, 0x03, 0x83, 64)
static const uint8_t configuration_ov2640[] = {CONFIGURATION(USB_CAM_MODES, USB_CAM_MODE_COUNT)};
static const uint8_t configuration_ov3660[] = {CONFIGURATION(USB_CAM_OV3660_MODES, USB_CAM_OV3660_MODE_COUNT)};
static const uint8_t configuration_discovery[] = {
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN, 0, 100),
    TUD_CDC_DESCRIPTOR(0, 5, 0x82, 8, 0x03, 0x83, 64)};
_Static_assert(sizeof(configuration_ov2640) == CONFIG_LENGTH(USB_CAM_MODES), "OV2640 length mismatch");
_Static_assert(sizeof(configuration_ov3660) == CONFIG_LENGTH(USB_CAM_OV3660_MODES), "OV3660 length mismatch");

static UsbCamProfile active_profile = USB_CAM_PROFILE_DISCOVERY;
UsbCamProfile usb_cam_usb_profile(void) { return active_profile; }
void usb_cam_usb_select_profile(UsbCamProfile profile) {
  active_profile = (unsigned)profile <= USB_CAM_PROFILE_OV3660 ? profile : USB_CAM_PROFILE_DISCOVERY;
}
const uint8_t* tud_descriptor_device_cb(void) { return (const uint8_t*)&devices[active_profile]; }
const uint8_t* tud_descriptor_configuration_cb(uint8_t index) {
  if (index != 0) return NULL;
  if (active_profile == USB_CAM_PROFILE_DISCOVERY) return configuration_discovery;
  return active_profile == USB_CAM_PROFILE_OV3660 ? configuration_ov3660 : configuration_ov2640;
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
    static const char* const products[] = {"ESP32 Camera Discovery", "ESP32 OV2640 USB Camera",
        "ESP32 OV3660 Legacy Camera", "ESP32 OV3660 USB Camera"};
    static const char* const video_names[] = {"Sensor unavailable", "OV2640 MJPEG Video",
        "OV3660 Legacy MJPEG Video", "OV3660 MJPEG Video"};
    const char* string = index == 2 ? products[active_profile] :
        index == 4 ? video_names[active_profile] : strings[index];
    count = strlen(string);
    if (count > 63) count = 63;
    for (size_t i = 0; i < count; ++i) descriptor[i + 1] = (uint8_t)string[i];
  }
  descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * count + 2));
  return descriptor;
}
