# Runtime USB camera implementation plan

> Use superpowers:executing-plans inline, with independent review before upload.

**Goal:** Select real sensor resolution/FPS from OBS and configure the ESP from
the Pico after one planned ESP upload.

**Architecture:** A shared versioned configuration travels back over hardware
SPI while the ESP sends frame/status headers. Pico UVC commits select modes;
generation and configuration CRC fence old frames. ESP applies complete settings
and reports failures without losing the command channel.

**Tech stack:** Existing pinned Arduino ESP32 SDK, Pico SDK/TinyUSB 0.18, C/C++,
PlatformIO; no dependency upgrade.

**Spec:** [usb-camera-runtime-design.md](usb-camera-runtime-design.md), approved.

## Global constraints

- Preserve checkpoint `usb-cam-first-working` and existing legacy ESP code.
- Keep GP8–GP14 unused; add GP7 to ESP GPIO2 only.
- Three 64 KiB JPEG slots, 8 MHz SPI, CRC-protected messages, bounded waits.
- Recurring settings belong to Pico; ESP supports SDK geometry through UXGA.
- Physical FPS is measured, not inferred from USB descriptors.

## Review focus

1. Failed/unsupported sensor settings never publish partially configured images.
2. Either board resets or generations repeat: compare token and generation.
3. Capture/oversize failure must not stop command polls.
4. USB mode changes cannot release driver-owned memory or submit old dimensions.
5. A long/invalid CDC command cannot overflow memory or block USB servicing.

## Tasks

- [ ] Shared contract: add `shared/usb_cam/configuration.h` (Config, Settings,
  `valid_config`, explicit encode/decode/token), replace fixed `profile.h` with
  C-compatible mode metadata, extend protocol Header to carry status, dimensions,
  interval, generation/token and sensor PID. Add failing-then-passing tests for
  malformed bytes, settings bounds, repeated generations, status-only frames,
  and matching frames to desired configuration. Keep slot ownership tests.
- [ ] ESP: `camera_control.h/.cpp` owns initialization, geometry/settings apply,
  confirmation and capture; `spi_link` exchanges headers full duplex; `main`
  polls independently of camera success and paces captures. Test real control
  code using a fake camera SDK: callback failures, defaults recovery, geometry
  verification and no framebuffer retained across reconfiguration. Build ESP.
- [ ] Pico receiver: TX DMA sends a snapshot of configuration on GP7, RX DMA
  retains existing accounting. Validate status separately from image data;
  synchronize desired state/stats through the existing critical section.
  Add a frame match helper test for mode/generation changes and old frames.
- [ ] Pico UVC and CDC: descriptor list from shared mode table, valid discrete
  intervals, VGA default; UVC commit updates desired config. Commands validate
  settings, report desired/applied/error state, never block. Add descriptor,
  parser overflow/invalid input, pacing and pinned-driver negotiation tests.
- [ ] Build/test/review: run `sh test/run-usb-cam-tests.sh`, build new ESP and
  Zero/Pico environments plus legacy, `git diff --check`, fresh independent
  reviewer. Fix important findings and commit before the intended ESP upload.
- [ ] Hardware: request precise programming/wiring actions, flash ESP once then
  Pico, confirm PID and config acknowledgments, decode each resolution, exercise
  tuning/default recovery and close/reopen. Tune offered rates on Pico only.
  Update `USB_CAMERA.md` with commands, measurements and remaining limitations.

## Execution record

- Shared protocol, settings, sensor controller, SPI return, UVC/CDC integration
  implemented. Host tests pass with ASan/UBSan, including actual SDK camera
  controller lifecycle and pinned TinyUSB negotiation. All five build targets
  (two ESP32, three Pico) passed. Independent source review found no substantive
  defects blocking upload. Its two suggested lifecycle coverage additions now
  pass: eight bad-geometry frames stop capture without confirmation, and failed
  oversize quality adaptation releases the framebuffer and reports an error;
  both recover through changed desired configuration. Hardware validation remains.
- Runtime commit `9087b4b` uploaded and verified on ESP32 (one upload) and
  RP2040-Zero on 2026-11-05. macOS enumerated all nine resolutions/rate choices.
  Kate was asked to remove ESP GPIO0-to-GND and programmer TX, restore GP3-to-ESP3,
  add GP7-to-ESP2, then power/reset. Await that physical step before video tests.
- Ruling: keep 60 FPS opt-in in `zero_camera_fast` until physical measurement;
  normal menu uses 30 FPS through SVGA and 15 FPS above it. This avoids presenting
  an unmeasured high-rate experiment as validated output.
- Ruling: SDK TinyUSB 0.18 discrete descriptors need explicit interval byte
  encoding; its multi-frame defaults and MJPEG size negotiation need an adapter.
  Reproduced default failure against the actual driver, then verified every
  mode/interval and invalid commits. No SDK package modifications.
- Ruling: all settings changes restart camera capture to restore unspecified
  sensor defaults deterministically. This costs a short pause but avoids stale
  settings after failures or changing presets.

Use new defaults `usb_camera` and `zero_camera` with a `pico_camera` variant;
the tagged checkpoint retains the prior fixed-profile firmware. Continue here
on the existing feature branch, as explicitly requested and approved.
