# USB camera modes selected by the computer

## Intent and checkpoint

Kate wants OBS and other UVC applications to list resolutions and frame rates,
and to switch the real sensor without flashing either board. Prepare the ESP32
for one planned upload: recurring camera tuning belongs to the Pico. The working
VGA version is commit `79d60ee`, tagged `usb-cam-first-working` (about 25 FPS on
the tested Mac). Continue in this existing repository and branch as requested.

## Approach

Use standard UVC frame descriptors and commit negotiation on the Pico. Add a
hardware SPI return channel, GP7 to ESP32 GPIO2, to carry a complete desired
configuration. Kate selected this approach. Existing SCK/data/CS/READY wiring
stays unchanged; RP2040 GP8–GP14 stay unused.

Alternatives considered: multiplex the READY wire with return data (more timing
and recovery complexity); reflash a fixed profile for each resolution (does not
meet the OBS requirement). A host-specific video application is unnecessary.

## User-visible behavior

Expose common sensor resolutions: 160x120, 320x240, 480x320, 640x480, 800x600,
1024x768, 1280x720, 1280x1024 and 1600x1200. VGA remains the default. These are
modes of the attached physical sensor, not interchangeable virtual sensors.
The ESP32 accepts every SDK JPEG frame size up to UXGA through geometry lookup,
so adding another supported size to the Pico later does not require ESP flashing.

Candidate requested rates are 5/10/15/20/25/30 FPS through SVGA, and 5/10/15 FPS
above SVGA. Test 60 FPS at the two smallest sizes as an optional candidate. The
delivered maximum must be measured: sensor timing, exposure, JPEG size, 8 MHz
SPI and full-speed USB all constrain it. Do not advertise measured performance
from descriptor values or promise maximum resolution and maximum FPS together.
Keep only useful, tested top rates after hardware verification; lower rates
remain selectable. A mode change may briefly pause video while old frames drain.

JPEG stays sensor-compressed. Three fixed buffers of at most 64 KiB each bound
Pico memory; actual JPEG allowance is also limited by the requested rate and a
conservative 720 KiB/s transport budget. Compression increases when necessary
to fit a frame. Report actual quality and oversize losses. High-resolution modes
may trade fine detail for throughput; no upscaling, duplicate frames or silently
substituted resolutions.

## ESP32 configuration contract

One versioned, CRC-protected fixed-size desired-configuration message contains
generation, width/height, frame interval, JPEG ceiling, initial quality and
optional sensor settings. Encode fields explicitly, not compiler struct layouts.
Pico snapshots this message before asserting READY. ESP clocks it in while
sending its packet header, using full-duplex hardware SPI. A separate TX DMA
channel feeds the Pico response. The existing RX DMA ownership rules remain.

The ESP also sends header-only status polls independently of frame capture.
Thus a missing frame, an oversize image, a stopped host or failed sensor startup
cannot prevent the Pico from changing settings. Reject malformed commands,
unsupported versions, dimensions, intervals and out-of-range settings without
applying them. Echo requested/applied generation, sensor PID and configuration
failure status, even when no image is available. Repeating the same configuration
is idempotent; after either controller resets, repeated desired state restores
synchronization. No command is allowed to exceed the compiled buffer ceiling.
Compare the complete desired configuration, not generation alone, after resets;
echo its CRC token as well as generation to distinguish reused boot generations.

Configuration is all-or-error: prevalidate every field and available callback
before mutation. On any callback failure, stop publishing images, retain the
last confirmed generation only as historical status, report the attempted
generation and error, and continue status polling. Do not label partially
configured images as either the old or the new mode. Keep the camera peripheral,
SCCB and UXGA-sized buffers initialized across requests. Apply resolution and
quality through sensor callbacks; unchanged settings and FPS-only requests need
no sensor writes. Optional-control changes (including `defaults`) and dirty-state
recovery reset only the sensor registers, restore the SDK defaults, refresh its
status cache, then apply JPEG format, geometry, quality and explicit controls.
Track successfully programmed state separately from desired state. No automatic
retry loop for an identical rejected request on the ESP; the Pico may issue at
most two new generations for fresh matching startup/capture/reset/clock failures,
with 250 ms cooldown. A null framebuffer ends the capture attempt with error 6,
keeping status polling responsive after the SDK's four-second timeout.
Advance confirmed generation only after mandatory settings succeed and a fresh
JPEG validates against requested geometry. Unsupported optional settings reject
that request explicitly; omitting them uses sensor defaults. This is the builder's
failure-policy decision following independent review.

Support recurring tuning through explicit sensor callbacks: quality,
brightness, contrast, saturation, sharpness/denoise when supported, mirror/flip,
automatic/manual exposure, exposure level/value, automatic/manual gain and
ceiling, white balance and mode, lens/gamma/pixel corrections, downsize and
colorbar. Leave unspecified settings at driver defaults. Report unsupported or
failed controls instead of silently claiming success. Permit camera XCLK choices
10/16/20/24 MHz through the sensor's live clock callback, followed by geometry
programming; keep the proven 20 MHz default until 24 MHz is measured. This is safe
for the pinned driver's JPEG DMA sampling path, which is independent of XCLK.
Track successful clock changes independently of the sensor's mutable cache and
report the actual LEDC frequency. The 24 MHz choice is an experiment, not an FPS
guarantee.
Do not expose arbitrary memory/register writes, PLL overclocking or firmware
upload through SPI. New firmware bugs or fundamentally new capabilities may
still require an ESP upload; routine settings do not.

Runtime settings are changed with bounded text commands on the Pico CDC port
(for example `set quality 16`, `set brightness 1`, `set xclk_mhz 20`, `defaults`).
They remain in RAM for this iteration; compiled defaults and the mode table
live on the Pico. OBS owns the resolution/FPS selection. Report accepted desired
state separately from confirmed applied state. Updating settings must not block
USB servicing. Application-specific exposure dialogs are outside this iteration;
resolution/FPS work through standard UVC.

## Switching and failure handling

UVC commit validates the mode and interval then updates desired state without
waiting for the ESP. New JPEG headers echo dimensions, interval and generation.
Do not submit old-mode or old-generation frames to the new USB stream. Return
every ESP camera framebuffer before changing sensor geometry, then discard
queued frames and validate JPEG SOF geometry before acknowledging the change.
Keep TinyUSB-owned buffers alive until the actual driver releases them. Preserve
the existing endpoint reopen, abort and watchdog recovery fixes.

Validate both the descriptor list and pinned TinyUSB probe/commit behavior;
multiple frame descriptors must negotiate their correct dimensions, intervals
and compressed buffer limits. A new prototype PID avoids stale single-mode host
descriptor caches. Do not upgrade the USB stack during this feature.

## Hardware and validation

Build and review the complete ESP firmware before requesting the one intended
upload; Kate has already put it in boot mode. Disconnect READY from ESP GPIO3
and leave the new GPIO2 return wire disconnected while flashing (GPIO2 must be
low or floating for the ESP bootloader). After flashing, disconnect programmer
TX, free GPIO0, restore READY, add GP7-to-GPIO2, and reset. Ask for these physical
steps precisely when needed. Pico may be flashed repeatedly as Kate authorized.

Host tests: malformed/partial configuration, invalid fields, CRC, generation
changes and reset recovery, status-only packets, old-mode rejection, frame
ownership, variable pacing, all descriptor dimensions/intervals, and actual
TinyUSB negotiation. Build ESP and Zero/Pico variants and retain legacy ESP code.
Independent review focuses on the ESP contract before upload and DMA/USB lifetime.
Hardware: verify sensor identity, change resolutions repeatedly using AVFoundation
or OBS, decode frames at every offered size, measure actual rates and losses,
check brightness/quality through Pico and restore defaults, then reopen VGA.
Record what was physically verified separately from build/test results.

## Primary sources inspected

- TinyUSB pinned SDK 0.18.0 `video_device.c`, `video.h`, `video_capture`, and
  [UVC callbacks](https://docs.tinyusb.org/en/latest/reference/class/video.html).
- [GB Interceptor](https://github.com/Staacks/gbinterceptor): bulk RP2040 UVC and
  adopting host-selected intervals; not a proof of our natural-scene rates.
- [Espressif OV2640 driver](https://github.com/espressif/esp32-camera/blob/master/sensors/ov2640.c)
  and locally installed SDK sensor API: geometry and sensor control callbacks.
- [Espressif CameraWebServer](https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP32/examples/Camera/CameraWebServer/app_httpd.cpp):
  runtime sensor controls as prior art, transported over SPI here instead of HTTP.
- [RP2040 datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf):
  GP7 SPI0 TX, full-duplex SPI/DMA and 264 KiB SRAM limits.
- [ESP32 boot mode](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/boot-mode-selection.html):
  GPIO2 must be low/floating during serial flashing.

Implementation details such as table layout, exact command field offsets,
diagnostic names and test harness structure are delegated to the builder.
