#pragma once

#ifndef USB_CAM_PROFILE
#define USB_CAM_PROFILE 1
#endif

#if USB_CAM_PROFILE == 1
#define USB_CAM_WIDTH 640
#define USB_CAM_HEIGHT 480
#define USB_CAM_FPS 30
#define USB_CAM_MAX_JPEG (24 * 1024)
#define USB_CAM_QUALITY 16
#elif USB_CAM_PROFILE == 2
#define USB_CAM_WIDTH 800
#define USB_CAM_HEIGHT 600
#define USB_CAM_FPS 15
#define USB_CAM_MAX_JPEG (48 * 1024)
#define USB_CAM_QUALITY 12
#elif USB_CAM_PROFILE == 3
#define USB_CAM_WIDTH 1280
#define USB_CAM_HEIGHT 720
#define USB_CAM_FPS 10
#define USB_CAM_MAX_JPEG (60 * 1024)
#define USB_CAM_QUALITY 16
#elif USB_CAM_PROFILE == 4
#define USB_CAM_WIDTH 320
#define USB_CAM_HEIGHT 240
#define USB_CAM_FPS 30
#define USB_CAM_MAX_JPEG (24 * 1024)
#define USB_CAM_QUALITY 12
#else
#error "USB_CAM_PROFILE must be 1 (VGA30), 2 (SVGA15), 3 (HD10), or 4 (QVGA30)"
#endif

#define USB_CAM_INTERVAL_100NS (10000000 / USB_CAM_FPS)

#ifdef __cplusplus
#include <cstdint>
namespace usb_cam {
struct Profile {
  uint16_t width;
  uint16_t height;
  uint8_t fps;
  uint32_t max_jpeg;
  uint8_t quality;
};
constexpr Profile kProfile{USB_CAM_WIDTH, USB_CAM_HEIGHT, USB_CAM_FPS,
                           USB_CAM_MAX_JPEG, USB_CAM_QUALITY};
}  // namespace usb_cam
#endif
