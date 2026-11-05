# USB POV camera implementation plan

Completed checkpoint plan. Current work continues in
[usb-camera-runtime-plan.md](usb-camera-runtime-plan.md).

Goal: build paired PlatformIO firmware for a measured USB-only camera prototype.
Spec: [usb-camera-design.md](usb-camera-design.md).
Execute inline; user authorized implementation and delegated routine choices.

1. Shared protocol and profiles (`shared/usb_cam/`), with host tests first:
   explicit byte encoding, CRC, JPEG geometry and bounded frame-slot ownership.
   Validate malformed lengths/profile mismatches, corruption, slow USB,
   disconnects and sequence wrap. Run `sh test/run-usb-cam-tests.sh`.
2. ESP32 transmitter (`src/usb_cam/`), separate PlatformIO environments:
   sensor JPEG capture, validated frames, DMA staging, bounded READY/CS waits,
   timeout recovery, configurable profiles and counters. Build all profiles.
3. RP2040 receiver (`pico/`): pinned native Pico SDK PlatformIO configuration,
   three-slot DMA receiver, dual-core synchronization, UVC/CDC descriptors and
   diagnostics. Build Zero profiles and the Pico variant. Host-check descriptors.
4. Integration: run existing servo tests and legacy build, review changes with a
   fresh reviewer, fix important findings, write build evidence and hardware
  acceptance instructions in `USB_CAMERA.md`. The original handoff left firmware
  unflashed; Kate subsequently requested and authorized hardware uploads.

Review focus: DMA/CS edge races; USB buffer lifetime across stop/unplug/reopen;
profile/dimension mismatch; sender/receiver reboot; boot pin and power conflicts.

Progress:
- Research and branch creation complete; baseline servo tests passed.
- Current scope is USB-only per user's POV quality priority, superseding the
  earlier simultaneous SD/Wi-Fi proposal. Existing legacy files remain available.
- Shared protocol, ESP32 sender, Pico receiver, UVC/CDC integration and user guide
  complete. All five ESP32 and five Pico build environments passed on 2026-11-05.
- Protocol, descriptor, timing/slot ownership and ISO restart host tests passed
  with address/undefined-behavior sanitizers; existing servo tests also passed.
- Independent source review completed. Its pending-ISO restart finding was fixed
  with an upstream-style abort adapter and a failing-then-passing regression test.
- Both boards were subsequently flashed and verified with VGA30 firmware. Pico
  UVC/CDC enumeration and diagnostics passed. The revised GP0-GP3/SPI0 receiver
  was also built, flashed and verified, leaving GP8-GP14 unused. Long-duration
  FPS, latency, motion quality and soak tests remain for Kate's hardware
  session using the acceptance procedure in `USB_CAMERA.md`.
- Wired bring-up reproduced and fixed DMA length accounting after CHAN_ABORT.
  Regression tests cover cleared transfer count and outstanding final writes.
- macOS isochronous capture failed with a USB pipe error. Bulk UVC, following
  GB Interceptor, decoded 192 frames in eight seconds at VGA; steady SPI and USB
  counters were approximately 25 FPS. USB endpoint reopen resets hardware plus
  both busy and claimed state, with a regression test for cancelled claims.
- The reviewed reopen fix passed three consecutive five-second decoded captures
  (122, 121 and 123 frames). SPI invalid/timeouts stayed zero; all five Pico
  builds and host tests passed. A live preview was started for Kate.
