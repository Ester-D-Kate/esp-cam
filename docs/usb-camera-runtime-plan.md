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
- After Kate reassembled/reset the boards, the updated ESP reported
  `control_error=2`, zero receive prefix/stored CRC, calculated CRC 366415372
  (124 zero bytes), at 1 MHz. Forward status remained valid, sensor PID zero.
  Kate confirmed the SD socket is empty. A temporary Pico-only test overrode
  GP7 output and output-enable during selected transactions in normal/high/low
  windows. GP7 readback was 1 for forced high and 0 for forced low; ESP received
  zeros in both windows. Log: `.pio/usb-cam-research/return-level-probe-live.log`.
  This separates the symptom from ordinary SPI edge timing but does not identify
  whether the fault is the wire/connector or the ESP input path. All temporary
  probe source was removed and the normal Pico image re-uploaded with verified
  flash. Next physical check: substitute a known-good GP7-to-IO2 connection.
- Kate identified the return wire on GP6 and moved it to GP7. Configuration was
  accepted immediately (CRC/token match, OV2640 PID 0x26); VGA USB decoding
  delivered 122 frames over five seconds, about 25 FPS steady state. The initial
  nine-mode sweep decoded UXGA/HD/SXGA/XGA at about 12.5 FPS, QQVGA near 30 FPS,
  and HVGA/VGA near 25 FPS, but three mode changes stopped with initialization
  error 2. Exact SDK initialization failure cause is not known from that code.
- Ten generation-only VGA retries (same quality16 and all other settings) each
  recovered after the first initialization failure. Add Pico-only recovery:
  matching fresh Status + Init error + attempted generation/token, 250 ms
  cooldown, at most two retries per changed UVC request or accepted CDC command.
  Automatic generations and duplicate UVC commits preserve the retry budget;
  success and mismatched status cancel cooldown. All other errors stay visible.
  Tests cover stale status, unchanged status count on a successful frame, retry
  bounds, old identities, unchanged setting values and microsecond clock wrap.
  Independent review found no blockers. Host suites and all five builds pass;
  automatic recovery and high-rate mode measurements are the next bench checks.
- Pico recovery commit `5976ace` was uploaded as the candidate fast-rate build.
  SVGA decoded 123 frames/5 seconds near25FPS and QVGA requested60 decoded249
  frames/5 seconds near50FPS. Subsequent init failures exhausted the two-retry
  budget and persisted across several requested modes; do not mark recovery as
  sufficient or mode switching as complete. Capture errors and oversize counters
  also rose during reconfiguration. Detailed SDK UART errors are now needed.
- Prepared optional `zero_camera_uart` Pico environment: read ESP U0T/GPIO1 via
  GP5 (UART1 RX, 115200) and forward bounded, nonblocking `ESP:` lines on USB CDC.
  Existing SPI wiring is retained. The temporary extra wire and power/reset are
  needed to capture/reproduce the startup failure without reflashing ESP32.

- UART evidence after Kate connected U0T->GP5: a generation-only VGA request
  logged successful configuration but stopped producing frames; the old controller
  stayed at error0 while fb_get timed out every four seconds. A subsequent request
  logged SCCB_Write timeout263 at sensor0x30/register0x3e, then camera probe failure.
  The next retry recovered VGA25. Logs: `runtime-uart-live.log` and
  `runtime-uart-restart.log` under `.pio/usb-cam-research/`.
- Revise the full-restart policy above: retain camera driver/SCCB/I2S and UXGA
  PSRAM buffers. In-place resolution/quality changes follow the pinned Arduino
  CameraWebServer. FPS/generation-only requests avoid sensor writes. Advanced
  control changes and dirty-state recovery use sensor reset, checked OV2640 SDK
  overlay, refreshed status, JPEG format, requested geometry/quality/controls.
  Any failure suppresses capture until a changed request; null fb_get now reports
  error6 instead of blocking indefinitely. The SDK's existing4-second timeout
  remains; Pico retries errors2/6/7/8 at most twice with250ms cooldown.
- Permit live XCLK10/16/20/24 and track success independently of the sensor cache.
  The pinned f0bb429 ESP32 JPEG DMA setup is XCLK-independent; CameraWebServer
  uses live set_xclk. Keep20MHz default. OV2640 datasheet lists24MHz typical, but
  its actual frequency/FPS still needs measurement. Header104/108/112 now report
  driver-start attempts, sensor-reset attempts and LEDC-reported frequency;
  bytes116..123 stay reserved. Upload the new Pico decoder before new ESP.
- Regression tests failed first on the old unnecessary driver restart, rejected
  24MHz command/config, unhandled frame/reset/clock recovery and rejected diagnostic
  extension. Revised USB suite passes with ASan/UBSan, servo suite passes, and both
  ESP/four Pico builds pass. New lifecycle tests exercise no-op/FPS/geometry/
  quality changes, explicit->default, partial callback failure, frame timeout,
  reset failure, clock-cache failure, bad JPEG, oversize callback failure and
  startup recovery. Hardware lifecycle validation and an ESP upload remain pending.
- Source checks for this revision:
  [pinned camera lifecycle](https://github.com/espressif/esp32-camera/blob/f0bb429/driver/esp_camera.c),
  [OV2640 callbacks](https://github.com/espressif/esp32-camera/blob/f0bb429/sensors/ov2640.c),
  [ESP32 JPEG DMA](https://github.com/espressif/esp32-camera/blob/f0bb429/target/esp32/ll_cam.c),
  [LEDC clock](https://github.com/espressif/esp32-camera/blob/f0bb429/target/xclk.c),
  [CameraWebServer](https://github.com/espressif/arduino-esp32/blob/2.0.17/libraries/ESP32/examples/Camera/CameraWebServer/app_httpd.cpp),
  [OV2640 datasheet](https://docs.espressif.com/projects/esp-dev-kits/en/latest/_static/esp32-s2-kaluga-1/datasheet/Camera_OV2640.pdf).
- Independent review caught a pinned OV2640 recovery edge case: its bank cache
  changes before a failed SCCB write is checked. A subsequent reset could skip
  the physical bank select and write COM7 into the wrong bank. Force a checked
  full-byte BANK_SEL=1 through public set_reg(0x1ff,0xff,1) before sensor reset.
  The regression models cached SENSOR/physical DSP and failed first selection;
  it failed before this fix and passed afterwards. This is a source-verified
  recovery defect, not proof of the original hardware startup failure's cause.
- Final independent review found no remaining important source blockers. The
  expanded CDC report stays within its1536-byte local buffer and2048-byte TX
  buffer; the new Pico accepts old ESP diagnostic zeros. Full USB/servo tests,
  both ESP builds and all four Pico builds pass. Upload Pico first, verify old
  ESP status compatibility, then request the physical ESP programming step.
- Lifecycle source saved as `4735f12`. Its `zero_camera_uart` image was uploaded
  through the working CDC `bootsel` command and picotool, with flash verification
  successful. Log: `.pio/usb-cam-research/persistent-camera-pico-upload.log`.
  New-format JSON was received from the Pico while old ESP firmware continued
  VGA near25FPS (`invalid=0`, desired/applied generation1). Camera-start/reset/
  actual-clock diagnostics are correctly zero until the new ESP is flashed.
  The reviewed ESP image is built and pending Kate's physical programming setup.
  ESP binary SHA256: `ccda58cf175620208d33123cd4c9aa8f3b495223f146803f161e0bcf4dc8af31`.
  Pico UF2 SHA256: `43ab778b148428a4e8de0d280a9b79a15875116649f83065c80f1e85d4687f61`.

- 2026-11-06, Kate requested testing the current OV2640 before considering
  OV3660. No firmware changes: ESP remains `d776503`, Pico `4735f12`. All nine
  eight-second decodes passed (1774 frames total), then 19 additional five-second
  mode changes passed (2340 frames total). Small 160x120/320x240 modes requested60
  delivered about50FPS; HVGA/VGA/SVGA about25; larger modes about12.5. No retries,
  invalid SPI packets or new capture errors; historical capture_errors14 stayed
  constant. The old initialization failure did not reproduce in this run.
- Follow-up 30-second UXGA and VGA decodes passed with372 and736 frames. UXGA
  needed quality50 and discarded oversize frames during adaptation. Across all
  these runs oversized rose0->112, then stayed112 throughout both sustained
  decode windows. VGA ended at quality16, generation30 applied/desired match.
  Logs: `ov2640-verification-2026-11-06.log`, `ov2640-switching-2026-11-06.log`,
  `ov2640-steady-2026-11-06.log` under `.pio/usb-cam-research/`. No footage saved.
  These short tests confirm current mode decoding, not resolution-switching
  reliability under all conditions or zero drops. The reviewed ESP lifecycle
  revision remains built but unflashed; its physical programming step is pending.

- 2026-11-06: Kate attached the ESP32-CAM to its programmer and explicitly
  requested the prepared firmware upload for an OV3660 test. Re-ran the complete
  USB host suite successfully, confirmed the prepared binary hash, and uploaded
  `usb_camera` through `/dev/cu.usbserial-140` at115200 baud. Upload completed in
  66.12 seconds; esptool verified every written region. Source revision4735f12;
  binary SHA256 `ccda58cf175620208d33123cd4c9aa8f3b495223f146803f161e0bcf4dc8af31`.
  Log: `.pio/usb-cam-research/ov3660-esp-upload-2026-11-06.log`.
  Pico already runs the matching revision with UART diagnostics. No new firmware
  changes were needed. Physical reassembly and live sensor identification are
  pending; do not claim OV3660 has been detected or measured yet. Both OV2640
  and OV3660 remain automatically supported by the same ESP image.

- Kate corrected the reassembly reply: the attached sensor is OV2640, not
  OV3660. After reconnection the ESP's UART log reaches Pico GP5 and shows the
  application running, but no SPI header/frame has arrived (status_packets0,
  rx_bytes0, sensor_pid0, camera_starts0). The Pico reports one receive timeout.
  Restarted its existing application through CDC bootsel + picotool reboot,
  without reflashing; the same state persisted. This precedes sensor setup,
  so it is not evidence of a camera initialization failure. Logs:
  `ov2640-new-firmware-startup-2026-11-06.log` and
  `ov2640-reconnect-restart-2026-11-06.log` under `.pio/usb-cam-research/`.
  Next physical check: ESP IO15->Pico GP1 chip select and ESP IO3->GP3 READY,
  with ESP removed from programmer and power off during wiring inspection.

- After Kate checked/reseated IO15->GP1 and IO3->GP3 and reset both boards, the
  same zero-byte/no-header condition persisted. Added a passive Pico-only
  `spi_gpio` diagnostic (GPIO input snapshot mask0x8f); all four Pico builds
  passed and `zero_camera_uart` was uploaded with verified flash. ESP firmware
  is unchanged. Six consecutive reports show GP0=1, GP1=0, GP2=0, GP3=0, GP7=0,
  with rx_bytes0, status_packets0 and a single receive timeout. This is consistent
  with chip select held low, preventing the receiver from rearming. It does not
  establish the exact misconnection or electrical fault. Logs:
  `ov2640-reseated-2026-11-06.log`, `reconnect-pin-level-build.log`,
  `reconnect-pin-level-upload.log`, `reconnect-pin-level-live.log` under the
  existing research directory. Next physical check: remake/trace all three SPI
  wires, IO13->GP0, IO15->GP1, IO14->GP2, retaining READY/return/common ground.

- After Kate remade IO13->GP0, IO15->GP1 and IO14->GP2, SPI resumed and the
  connected OV2640 reported PID 0x26. ESP remains at lifecycle commit `4735f12`;
  Pico remains at `e7ea38d`. Both nine-mode sweeps passed (20 MHz: 1,775 decoded
  frames; requested 24 MHz: 2,034). Actual clock is 23,981,264 Hz at requested
  24 MHz. Three no-op quality
  requests avoided resets; brightness override/default caused two sensor resets
  while camera_starts stayed 1. No capture errors or configuration retries.
- A repeated mode test conflicted with OBS changing formats and locking the
  device. Confirmed the overlapping OBS log and active process; Kate closed OBS.
  Ten repeated four-second large/small mode tests then passed without firmware
  changes. Do not interpret the earlier harness's process exit 0 as a pass: its
  individual mode results included failures, and the new gap-measurement tool
  returns failure on unsuccessful decode or timeout.
- Kate requested autonomous investigation of high-resolution pauses while away.
  Reproduced 1600x1200/15 receiving no video at both 24 MHz (119 seconds between
  diagnostic samples, 1,753 oversized discards) and 20 MHz (1,113 discards). In both intervals
  received/USB frame counters were fixed; capture errors, invalid packets, SPI
  timeouts and USB restarts did not increase. Source rejects JPEGs over 49,151
  bytes for requested 15 FPS, even after adaptive quality reaches 50. This explains
  a prolonged frozen image while configuration/status still appear healthy.
- Selecting 1600x1200/10 raises max_jpeg to 65,536 without reflashing. A 90-second
  UYVY decode delivered 873 frames/~9.68 FPS, quality 28, zero further oversized
  discards after adaptation, but maximum 369 ms arrival/401 ms PTS gaps. Separate
  90-second 1024p/15 and 720p/15 decodes delivered 1,108 and 1,185 frames (~12.33 and
  13.17 FPS), maximum 205/240 ms arrival gaps. All their steady-window transport,
  capture and oversized counters remained unchanged. These are not zero-drop
  or moving-POV guarantees; see USB_CAMERA.md for the fuller measurements.
- New measurement script and timestamp/counter JSON files are under Kate's
  requested `~/Desktop/Codex-projects/esp-cam-validation/` directory. No audio
  or footage is stored. Existing short-sweep logs remain in the ignored research
  directory. Firmware has not changed during these measurements; the default
  clock is still 20 MHz, and the 24 MHz selection is volatile Pico-controlled state.
- Completed OBS-style NV12 checks through FFmpeg/AVFoundation at 24 MHz:
  UXGA/10: 884 frames/90 s (~9.80 FPS, maximum 289 ms arrival gap);
  VGA/30: 2,567/90 s (~28.47 FPS, 116 ms); QVGA/60: 5,105/90 s (~56.70 FPS, 47 ms);
  SVGA/25: 1,753/90 s (~19.45 FPS, 144 ms); SVGA/30: 1,627/60 s (~27.10 FPS, 127 ms).
  Every steady window had unchanged discard/error/restart counters. These are
  host-decoder measurements, not OBS-internal FPS or end-to-end latency.
- The longer SVGA/30 check completed 300 seconds with 8,173 decoded frames (~27.24 FPS),
  largest steady arrival gap 132 ms and PTS gap 161 ms, no gaps over 250 ms, and no
  changes to oversized/capture/invalid/SPI-timeout/USB-restart/sequence-gap/
  replaced counters throughout. Camera starts 1 and sensor resets 2 stayed fixed.
  Results and practical OBS presets are recorded in USB_CAMERA.md. A moving
  wearable test remains outside this unattended bench check. The saved tool now
  has uv metadata with pinned pyserial 3.5 and was verified through uv.
- Final VGA/30 check through uv decoded 231 frames in eight seconds. Left runtime
  configuration at VGA/30, requested 24 MHz, quality 16, generation 63 confirmed.

Use new defaults `usb_camera` and `zero_camera` with a `pico_camera` variant;
the tagged checkpoint retains the prior fixed-profile firmware. Continue here
on the existing feature branch, as explicitly requested and approved.

## UXGA stall repair — 2026-11-06

Goal: remove the reproduced rejection of every UXGA frame above 48 KiB at
requested 15 FPS, using the existing ESP protocol and a Pico-only upload.
Kate has authorized autonomous fixes and requested avoiding more ESP uploads.
Use the existing C++/PlatformIO implementation and branch, without new wiring.

The selected approach allows the existing 64 KiB frame capacity for UXGA at all
its advertised rates. Smaller modes retain their rate-dependent compression
budgets, which produced better motion performance in the benchmark. This does
not promise 15 FPS for large JPEGs or support JPEGs over 64 KiB. Increasing all
mode budgets would sacrifice lower-mode FPS; increasing buffer capacity would
also require an ESP upload and a different Pico memory layout, so neither is
part of this repair.

- [x] Add a real camera-control regression in `test_camera_control.cpp`: a UXGA
  preset must forward valid 52 KiB and 64 KiB JPEGs, return each buffer, and reject
  64 KiB + 1. Observe failure with the current 48 KiB allowance.
- [x] Update `mode_config()` in `shared/usb_cam/configuration.h` for UXGA only;
  retain the 64 KiB wire validation and smaller-mode budgets. Update the existing
  configuration budget assertion to cover the intentional UXGA exception.
- [x] Run the USB host suite, servo suite and all four Pico builds; check the
  source change and binary compatibility before uploading only the Pico.
- [x] Re-run UXGA/15 and UXGA/10 gap measurements. Compare host
  `drop_late_frames` settings separately, without interpreting buffering as a
  hardware fix or a proven latency improvement.
- [ ] Complete an uninterrupted five-minute soak and repeat the smaller-mode
  sweep after the bulk fix. Deferred because another client changed the first
  soak's mode and OBS was running when the second attempt started. Leave the
  active OBS preview available; do not report either attempt as a soak pass.
- [x] Record measured results and remaining limits for the repair commit.

Review correction: UXGA's advertised maximum bitrate must cover 65,536 bytes
per 666,666 * 100 ns, rounded upward to 7,864,328 bit/s. The descriptor regression
failed on the old 5,898,240 value before the correction. Other modes retain that
original maximum. No buffer allocation or ESP protocol layout changes.

Ruling: add a Pico-only `usb_gap_us` diagnostic to locate the remaining brief
pauses. Disabling AVFoundation late-frame dropping did not remove them (373 ms
maximum arrival gap at UXGA/10). Per-second maximum USB-completion gaps will
distinguish device delivery gaps from later host/decoder gaps. This adds a few
bytes of state and one JSON field, with timer-wrap/report-boundary/reset tests;
it does not change streaming or require an ESP upload.

The timing baseline decoded 585 frames in 60 seconds (~9.74 FPS), with five
host gaps around 300 ms. During those same intervals, Pico completion gaps were
about 100 ms. The pinned TinyUSB 0.18 driver completes a frame without appending
a ZLP when its final payload is smaller than 2,048 bytes but a multiple of the
64-byte USB endpoint packet size. This is a reproducible protocol defect and a
candidate explanation for host-only gaps. Relevant prior implementation discussion:
[Linux UVC bulk URB sizing](https://www.mail-archive.com/linux-uvc-devel%40lists.berlios.de/msg03790.html);
vendor background: [SEGGER bulk zero-packet control](https://doc.segger.com/UM09001_emUSBD.html#USBD_BULK_WriteEx()).

Ruling: correct that final-payload case in the existing version-guarded video
adapter. Preserve SDK ownership until the ZLP completes; a failed claim,
submission or completion retains ownership for existing timeout recovery. A new
COMMIT aborts the endpoint through the existing adapter before releasing it.
No extra pending flag or SDK source edit is needed. Actual-driver host tests
failed on a 62-byte frame (64-byte payload) before the repair, then passed for
short/full/multi-payload frames, ZLP failure paths and pending-ZLP cancellation.
Read-only independent review found no actionable implementation issue and
requested the separate direct-cancellation case, which was added and passed.
All four Pico builds and an ESP compatibility build pass; ESP was not uploaded.

Post-repair 90-second runs: UXGA/10 decoded 898 frames (~9.98 FPS, max arrival
gap 203 ms); UXGA/15 decoded 900 (~9.99 FPS, 189 ms). Neither had a steady gap
above 250 ms. The former had one further adaptive oversize discard; the latter
had one invalid SPI header and one receive timeout, with automatic recovery.
Capture errors and USB restarts did not increase in either active run.

The first 300-second attempt is not a soak pass: after 257 seconds, a second
host client changed the real stream to 720p. OS camera-service logs confirm
the format change and added client; Pico's generation/mode changed without
USB recovery. FFmpeg retained the old expected raw-buffer geometry and rejected
the smaller frames. The app was not identified or closed. The client subsequently
stopped. Updated the existing Desktop measurement tool to stop and label mode
interference or ten seconds without decoded frames, and report trailing silence.
A separate default-clock (20 MHz) UXGA/15 run then passed 60 seconds at ~9.54 FPS,
maximum 180 ms arrival gap and unchanged steady error/discard/restart counters.

The final repeat/sweep command stopped before opening the camera because OBS
was running. Its properties dialog showed MJPEG Video at 1280x720, requested
15 FPS, buffering enabled. No OBS settings were changed and the preview was
left available. The earlier 90-second UXGA/10 and /15 and default-clock checks
are completed passes; an uninterrupted five-minute post-repair soak and new
smaller-mode hardware sweep remain unverified. The host suite covers every
advertised mode/interval and both menu variants. This boundary is documented
in USB_CAMERA.md rather than treating the interrupted soak as a firmware pass.

## First physical OV3660 confirmation — 2026-11-06

Kate fitted OV3660. CDC and forwarded ESP logs confirm sensor PID 0x3660 with
the existing ESP lifecycle image and Pico `54061c3` firmware; no upload or
runtime setting change was necessary. OBS was active, so no competing video
capture was started. A 60-second read-only diagnostic collection contains
59.002 seconds between first/last samples, 737 received/USB-completed frames
(12.49 FPS), at 1600x1200/requested 15 FPS, 20 MHz, quality 28. Maximum USB
completion gap was 136.025 ms. Mode and confirmed configuration stayed constant;
capture/oversize/invalid/SPI-timeout/USB-restart/retry/start/reset counter deltas
were zero. Saved evidence: Desktop validation directory,
`20261106-033606-ov3660-live-obs.json`. This establishes detection and device-side
streaming for this mode, not all-mode support, host-decoded FPS or long-term
stability. OBS and the installed firmware were left unchanged.

## OV3660 performance research — 2026-11-06

Kate requested investigation of the datasheet's higher rates. Verified the
provided datasheet table 2-2, Espressif issues 232/221, driver PLL/readout code,
continuous-buffer guidance, and RP2040 full-speed USB specification. Results,
source links, bandwidth arithmetic and current compiled protocol limits are
recorded in USB_CAMERA.md. Wi-Fi is already off and two JPEG buffers already
enable continuous capture; neither wireless removal nor a third buffer proves
the sensor's advertised maxima can reach USB. Existing menus also limit the
requested rate, separately from sensor readout timing.

Ran bounded Pico-controlled XCLK comparisons while OBS retained the same UXGA/15
mode. Clock changes reset adaptive quality, confounding the first comparison;
the repeat still adapted once. Results: 20 MHz/quality28 ~13.80 USB FPS versus
24 MHz/quality16 ~11.07; repeat 20 MHz/quality16 ~9.32 versus 24 MHz/quality16–18
~10.97. No capture/invalid/SPI-timeout/USB-restart increments; one oversize
discard in the repeat 24 MHz window. Restored original 20 MHz, without flashing
or changing OBS. This is investigation, not acceptance of 24 MHz as a new
default. Adaptive quality history resets during these operations.

Research conclusion: start with measured VGA/60 and 720p/30 experiments through
the Pico's existing <=60-FPS contract, retaining actual-rate/error validation.
Those rates are targets, not capabilities established by this turn. Custom
OV3660 timing/cropping profiles, 1080p/3MP and 120-FPS requests exceed the
installed ESP contract and would need a deliberately prepared ESP capability
update. No such firmware change or upload was made during this feasibility
investigation. Avoid copying an old issue's PLL recipe as a validated preset.

Follow-up comparison held quality 28 explicitly at both clocks: 20 MHz delivered
12.55 USB FPS over 30.999 s with mean 50,186-byte JPEGs; 24 MHz delivered 11.13
over 30.001 s with mean 50,392-byte JPEGs. Neither added capture/oversize/invalid/
SPI-timeout/USB-restart errors. Short uncontrolled-scene evidence supports
retaining 20 MHz, not promoting 24 MHz or claiming a known cause of the rate
drop. Finished at 20 MHz with explicit quality override 28, matching the prior
compression level for this UXGA mode. The override persists across mode changes;
documented the difference from prior adaptive quality and the `defaults`/reboot
reset behavior. No firmware upload. Evidence:
`20261106-034858-ov3660-fixed-quality-clock.json` in the Desktop validation folder.
