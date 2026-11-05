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
- Three 64 KiB JPEG slots, 8 MHz JPEG SPI / 1 MHz control exchange in the current
  diagnostic update, CRC-protected messages, bounded waits.
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
- Return-path bench follow-up: Kate restored normal wiring and reseated GP7 to
  ESP2. The forward 128-byte status path works, but ESP `command_errors` rises
  on every configuration and sensor initialization never starts. Passive Pico
  input sampling observed the correct full 128-byte CFG2 response at GP7, with
  1024 clock edges and zero byte mismatches. Faster GPIO slew and a one-bit
  advance did not fix acceptance. Transient forward corruption seen during
  earlier diagnostic boots later disappeared; its cause is not established.
- Removed all temporary PIO sampling and bit-shift code. The next ESP image
  reports receive prefix, both CRCs, precise decode error and control clock over
  the working status path; RX memory is prefilled with 0xa5 before DMA. Its
  128-byte control exchange runs at 1 MHz, leaving JPEG chunks at 8 MHz. This is
  a diagnostic/conservative timing change, not a proven hardware fix. Pico gains
  a `bootsel` serial command, already exercised successfully on the bench.
- The status diagnostic round-trip test failed against the previous header
  decoder, and the real SPI transport host test failed against the old all-8-MHz
  exchange. Updated host tests and both ESP / all three Pico builds pass.
  Another ESP upload and full runtime-mode bench acceptance remain necessary;
  avoid reporting the checkpoint's 25 FPS as a runtime-mode result.
- Independent review found no blockers in the diagnostic update, including
  clock switching, manual CS, failure cleanup, header bounds, and the CDC
  diagnostic buffer. The reviewed Pico image was uploaded with flash verification
  and successfully read the old ESP's status packets (`invalid=0`); ESP receive
  fields remain zero until its update. The ESP has not been reflashed again yet.
- Diagnostic commit `d776503` was subsequently uploaded to ESP32 after Kate
  removed the entire board and seated it in her programmer. Port
  `/dev/cu.usbserial-140`, 115200 baud, upload completed in 66.37 seconds with
  every flash-region hash verified. Log:
  `.pio/usb-cam-research/return-final-esp-upload.log`.
  ESP application SHA-256:
  `4afc7f55c86cf1b9b92e7fd882e136a0e5ab8a4ef175163428db97ca9284d971`.
  Pico UF2 SHA-256:
  `79d51b74ea2e91827a36ab70f4859dd533bd4075b6d5305303c658bd2767d366`.
  Both uploads are verified; reassembly and live configuration/video testing
  are still pending. Future physical instructions must account for this seated
  programmer: remove the whole ESP board rather than assuming loose UART wires.

Use new defaults `usb_camera` and `zero_camera` with a `pico_camera` variant;
the tagged checkpoint retains the prior fixed-profile firmware. Continue here
on the existing feature branch, as explicitly requested and approved.
