#pragma once
#include <stddef.h>
#include <stdint.h>
#define USB_CAM_BUFFER_BYTES (112u * 1024u)
#define USB_CAM_LEGACY_BUFFER_BYTES (64u * 1024u)
#define USB_CAM_BYTE_BUDGET (720u * 1024u)
#define USB_CAM_MODE_COUNT 9
#define USB_CAM_DEFAULT_MODE 1
#define USB_CAM_INTERVAL(fps) (10000000u / (fps))
#define USB_CAM_USES_FULL_SLOT(w, h) ((w) >= 1600u && (h) >= 1080u)
// Full-resolution modes use the full slot; other modes retain the normal byte-rate budget.
// Round upward because discrete frame intervals are integer 100 ns units.
#define USB_CAM_MAX_BITRATE_INTERVAL(w, h, interval) \
  (USB_CAM_USES_FULL_SLOT(w, h) ? \
   (uint32_t)(((uint64_t)USB_CAM_LEGACY_BUFFER_BYTES * 8u * 10000000u + (interval) - 1u) / (interval)) : \
   USB_CAM_BYTE_BUDGET * 8u)
// Rates are requests, not measurements. Enable 60 FPS on the Pico only after
// hardware characterization; the ESP protocol already accepts this interval.
#ifndef USB_CAM_ENABLE_60FPS
#define USB_CAM_ENABLE_60FPS 0
#endif
#if USB_CAM_ENABLE_60FPS
#define USB_CAM_FAST_INTERVALS USB_CAM_INTERVAL(60), USB_CAM_INTERVAL(30), USB_CAM_INTERVAL(25), USB_CAM_INTERVAL(20), USB_CAM_INTERVAL(15), USB_CAM_INTERVAL(10), USB_CAM_INTERVAL(5)
#else
#define USB_CAM_FAST_INTERVALS USB_CAM_INTERVAL(30), USB_CAM_INTERVAL(25), USB_CAM_INTERVAL(20), USB_CAM_INTERVAL(15), USB_CAM_INTERVAL(10), USB_CAM_INTERVAL(5)
#endif
#define USB_CAM_NORMAL_INTERVALS USB_CAM_INTERVAL(30), USB_CAM_INTERVAL(25), USB_CAM_INTERVAL(20), USB_CAM_INTERVAL(15), USB_CAM_INTERVAL(10), USB_CAM_INTERVAL(5)
#define USB_CAM_LARGE_INTERVALS USB_CAM_INTERVAL(15), USB_CAM_INTERVAL(10), USB_CAM_INTERVAL(5)
#define USB_CAM_COMMON_MODES(X, VGA_RATES, SMALL_RATES, HD_RATES, XGA_RATES, UXGA_QUALITY) \
  X(1, 640, 480, 16, 30, VGA_RATES) \
  X(2, 800, 600, 12, 30, USB_CAM_NORMAL_INTERVALS) \
  X(3, 1280, 720, 16, 15, HD_RATES) \
  X(4, 320, 240, 12, 30, SMALL_RATES) \
  X(5, 160, 120, 12, 30, SMALL_RATES) \
  X(6, 480, 320, 12, 30, USB_CAM_NORMAL_INTERVALS) \
  X(7, 1024, 768, 16, 15, XGA_RATES) \
  X(8, 1280, 1024, 16, 15, USB_CAM_LARGE_INTERVALS) \
  X(9, 1600, 1200, UXGA_QUALITY, 15, USB_CAM_LARGE_INTERVALS)
#define USB_CAM_MODES(X) USB_CAM_COMMON_MODES(X, USB_CAM_NORMAL_INTERVALS, USB_CAM_FAST_INTERVALS, USB_CAM_LARGE_INTERVALS, USB_CAM_LARGE_INTERVALS, 16)
// Measured OV3660 readout limits apply to both normal and fast builds.
#define USB_CAM_HD_INTERVALS USB_CAM_INTERVAL(20), USB_CAM_INTERVAL(15), USB_CAM_INTERVAL(10), USB_CAM_INTERVAL(5)
#define USB_CAM_OV3660_BASE(X) USB_CAM_COMMON_MODES(X, USB_CAM_NORMAL_INTERVALS, USB_CAM_NORMAL_INTERVALS, USB_CAM_HD_INTERVALS, USB_CAM_NORMAL_INTERVALS, 28)
// QXGA was measured on hardware but cannot sustain frames within our 64 KiB
// slots, even at quality 50. Do not advertise a mode that freezes the stream.
#define USB_CAM_OV3660_MODES(X) \
  USB_CAM_OV3660_BASE(X) \
  X(10, 1920, 1080, 28, 10, USB_CAM_LARGE_INTERVALS)
#define USB_CAM_OV3660_MODE_COUNT 10
// Candidate sensor ceilings. Actual delivery is limited by exposure, JPEG size,
// ESP capture, SPI and USB. Keep conservative defaults even in these full menus.
#define USB_CAM_RATES_60 USB_CAM_INTERVAL(60), USB_CAM_INTERVAL(50), USB_CAM_INTERVAL(45), USB_CAM_NORMAL_INTERVALS
#define USB_CAM_RATES_120 USB_CAM_INTERVAL(120), USB_CAM_INTERVAL(90), USB_CAM_RATES_60
#define USB_CAM_RATES_45 USB_CAM_INTERVAL(45), USB_CAM_NORMAL_INTERVALS
#define USB_CAM_SMALL_SIZES(X, FIRST, RATES) \
  X(FIRST, 96, 96, 12, 30, RATES) \
  X(FIRST+1, 176, 144, 12, 30, RATES) \
  X(FIRST+2, 240, 176, 12, 30, RATES) \
  X(FIRST+3, 240, 240, 12, 30, RATES)
#define USB_CAM_OV2640_FULL_MODES(X) \
  USB_CAM_COMMON_MODES(X, USB_CAM_NORMAL_INTERVALS, USB_CAM_RATES_60, USB_CAM_LARGE_INTERVALS, USB_CAM_LARGE_INTERVALS, 16) \
  USB_CAM_SMALL_SIZES(X, 10, USB_CAM_RATES_60) \
  X(14, 400, 296, 12, 30, USB_CAM_RATES_60)
#define USB_CAM_OV3660_FULL_MODES(X) \
  X(1, 640, 480, 16, 30, USB_CAM_RATES_60) \
  X(2, 800, 600, 12, 30, USB_CAM_RATES_45) \
  X(3, 1280, 720, 16, 15, USB_CAM_RATES_45) \
  X(4, 320, 240, 12, 30, USB_CAM_RATES_120) \
  X(5, 160, 120, 12, 30, USB_CAM_RATES_120) \
  X(6, 480, 320, 12, 30, USB_CAM_RATES_60) \
  X(7, 1024, 768, 16, 15, USB_CAM_RATES_45) \
  X(8, 1280, 1024, 16, 15, USB_CAM_LARGE_INTERVALS) \
  X(9, 1600, 1200, 28, 15, USB_CAM_LARGE_INTERVALS) \
  X(10, 1920, 1080, 28, 10, USB_CAM_HD_INTERVALS) \
  X(11, 2048, 1536, 32, 5, USB_CAM_LARGE_INTERVALS) \
  USB_CAM_SMALL_SIZES(X, 12, USB_CAM_RATES_120) \
  X(16, 400, 296, 12, 30, USB_CAM_RATES_60)
#define USB_CAM_OV2640_FULL_MODE_COUNT 14
#define USB_CAM_OV3660_FULL_MODE_COUNT 16
#define USB_CAM_CAP_SENSOR_PROFILES 1u
#define USB_CAM_CAP_FULL_MODES 2u
typedef enum {
  USB_CAM_PROFILE_DISCOVERY, USB_CAM_PROFILE_OV2640,
  USB_CAM_PROFILE_OV3660_LEGACY, USB_CAM_PROFILE_OV3660,
  USB_CAM_PROFILE_OV2640_FULL, USB_CAM_PROFILE_OV3660_FULL
} UsbCamProfile;
static inline int usb_cam_full_profile(UsbCamProfile profile) {
  return profile == USB_CAM_PROFILE_OV2640_FULL || profile == USB_CAM_PROFILE_OV3660_FULL;
}
static inline uint32_t usb_cam_profile_capacity(UsbCamProfile profile) {
  return usb_cam_full_profile(profile) ? USB_CAM_BUFFER_BYTES : USB_CAM_LEGACY_BUFFER_BYTES;
}
static inline UsbCamProfile usb_cam_profile_for(uint16_t pid, uint32_t capabilities) {
  if (capabilities & USB_CAM_CAP_FULL_MODES) {
    if (pid == 0x26) return USB_CAM_PROFILE_OV2640_FULL;
    if (pid == 0x3660) return USB_CAM_PROFILE_OV3660_FULL;
  }
  if (pid == 0x26) return USB_CAM_PROFILE_OV2640;
  if (pid == 0x3660) return capabilities & USB_CAM_CAP_SENSOR_PROFILES ?
      USB_CAM_PROFILE_OV3660 : USB_CAM_PROFILE_OV3660_LEGACY;
  return USB_CAM_PROFILE_DISCOVERY;
}
typedef struct {
  uint16_t width, height;
  uint8_t quality;
  uint32_t default_interval;
  uint8_t interval_count;
  uint32_t intervals[11];
} UsbCamMode;
#define USB_CAM_TABLE_ROW(id, w, h, q, fps, ...) \
  {w, h, q, USB_CAM_INTERVAL(fps), sizeof((uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t), {__VA_ARGS__}},
static const UsbCamMode usb_cam_modes[] = {USB_CAM_MODES(USB_CAM_TABLE_ROW)};
static const UsbCamMode usb_cam_ov3660_modes[] = {USB_CAM_OV3660_MODES(USB_CAM_TABLE_ROW)};
static const UsbCamMode usb_cam_ov2640_full_modes[] = {USB_CAM_OV2640_FULL_MODES(USB_CAM_TABLE_ROW)};
static const UsbCamMode usb_cam_ov3660_full_modes[] = {USB_CAM_OV3660_FULL_MODES(USB_CAM_TABLE_ROW)};
#undef USB_CAM_TABLE_ROW
static inline const UsbCamMode* usb_cam_mode(unsigned index) {
  return index >= 1 && index <= USB_CAM_MODE_COUNT ? &usb_cam_modes[index - 1] : NULL;
}
static inline int usb_cam_valid_interval(const UsbCamMode* mode, uint32_t interval) {
  if (!mode) return 0;
  for (unsigned i = 0; i < mode->interval_count; ++i)
    if (interval == mode->intervals[i]) return 1;
  return 0;
}

static inline unsigned usb_cam_profile_count(UsbCamProfile profile) {
  if (profile == USB_CAM_PROFILE_OV2640_FULL) return USB_CAM_OV2640_FULL_MODE_COUNT;
  if (profile == USB_CAM_PROFILE_OV3660_FULL) return USB_CAM_OV3660_FULL_MODE_COUNT;
  if (profile == USB_CAM_PROFILE_OV3660) return USB_CAM_OV3660_MODE_COUNT;
  if (profile == USB_CAM_PROFILE_OV2640 || profile == USB_CAM_PROFILE_OV3660_LEGACY) return USB_CAM_MODE_COUNT;
  return 0;
}
static inline const UsbCamMode* usb_cam_profile_mode(UsbCamProfile profile, unsigned index) {
  if (!index || index > usb_cam_profile_count(profile)) return NULL;
  if (profile == USB_CAM_PROFILE_OV2640_FULL) return &usb_cam_ov2640_full_modes[index-1];
  if (profile == USB_CAM_PROFILE_OV3660_FULL) return &usb_cam_ov3660_full_modes[index-1];
  return profile == USB_CAM_PROFILE_OV3660 ? &usb_cam_ov3660_modes[index - 1] : usb_cam_mode(index);
}
// Pico descriptor state. Change only while TinyUSB is deinitialized.
#ifdef __cplusplus
extern "C" {
#endif
UsbCamProfile usb_cam_usb_profile(void);
void usb_cam_usb_select_profile(UsbCamProfile profile);
#ifdef __cplusplus
}
#endif
