#pragma once
#include <stddef.h>
#include <stdint.h>
#define USB_CAM_BUFFER_BYTES (64u * 1024u)
#define USB_CAM_BYTE_BUDGET (720u * 1024u)
#define USB_CAM_MODE_COUNT 9
#define USB_CAM_DEFAULT_MODE 1
#define USB_CAM_INTERVAL(fps) (10000000u / (fps))
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
#define USB_CAM_MODES(X) \
  X(1, 640, 480, 16, 30, USB_CAM_NORMAL_INTERVALS) \
  X(2, 800, 600, 12, 30, USB_CAM_NORMAL_INTERVALS) \
  X(3, 1280, 720, 16, 15, USB_CAM_LARGE_INTERVALS) \
  X(4, 320, 240, 12, 30, USB_CAM_FAST_INTERVALS) \
  X(5, 160, 120, 12, 30, USB_CAM_FAST_INTERVALS) \
  X(6, 480, 320, 12, 30, USB_CAM_NORMAL_INTERVALS) \
  X(7, 1024, 768, 16, 15, USB_CAM_LARGE_INTERVALS) \
  X(8, 1280, 1024, 16, 15, USB_CAM_LARGE_INTERVALS) \
  X(9, 1600, 1200, 16, 15, USB_CAM_LARGE_INTERVALS)
typedef struct {
  uint16_t width, height;
  uint8_t quality;
  uint32_t default_interval;
  uint8_t interval_count;
  uint32_t intervals[7];
} UsbCamMode;
#define USB_CAM_TABLE_ROW(id, w, h, q, fps, ...) \
  {w, h, q, USB_CAM_INTERVAL(fps), sizeof((uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t), {__VA_ARGS__}},
static const UsbCamMode usb_cam_modes[] = {USB_CAM_MODES(USB_CAM_TABLE_ROW)};
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
