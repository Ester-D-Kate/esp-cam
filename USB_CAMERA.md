# ESP32-CAM + RP2040-Zero USB camera

The current firmware exposes one MJPEG USB camera with resolution/FPS selection
in OBS and other UVC applications. The Pico sends the selected mode and sensor
settings back to the ESP32 over SPI. Changing resolution or recurring settings
requires no ESP32 reflash. Both controllers need this protocol-v2 firmware;
protocol-v1 firmware is deliberately rejected.

The first working VGA firmware is safely committed at `79d60ee`, tagged
`usb-cam-first-working`. It delivered approximately 24.9–25.0 FPS on the tested
Mac and passed three close/reopen captures. See the
[historical guide](docs/usb-camera-first-working.md) for those exact artifacts
and measurements. Runtime mode hardware results are recorded below as tested.

## Wiring

GPIO numbers below are board labels, not physical connector positions.

| ESP32-CAM | RP2040-Zero | Signal |
|---|---|---|
| GPIO14 | GP2 | SPI0 clock |
| GPIO13 | GP0 | SPI0 RX: ESP camera data to Pico |
| GPIO15 | GP1 | Active-low SPI chip select |
| GPIO3 / U0R | GP3 | Pico READY handshake |
| **GPIO2** | **GP7** | **New SPI0 TX return wire: Pico configuration to ESP** |
| GND | GND | Shared ground |

The five signal wires use 3.3 V logic, hardware SPI0 and RX/TX DMA, mode 1.
The current source uses 1 MHz for the 128-byte configuration/header exchange
and 8 MHz for JPEG data, under the same chip select. This adds about 0.9 ms per
packet compared with an all-8-MHz exchange. Pico GP8–GP14 remain unused.
GP3 is a normal GPIO handshake. Keep wiring
short. Remove the SD card and disconnect servo/gyro wiring for this USB build.
USB firmware excludes Wi-Fi and recording; the legacy `esp32cam` environment
retains the original application and recording-button behavior.

Power Pico over USB and supply stable 5 V to ESP32. Share GND; do not connect two
independent 5 V supplies together or power the ESP32 from Pico 3V3.

## Build and upload

Open `usb-camera.code-workspace` in VS Code with PlatformIO. Build:

```sh
pio run -e usb_camera
pio run -d pico -e zero_camera
```

For a full-size Raspberry Pi Pico use `pico_camera` instead of `zero_camera`.
The native Pico SDK platform, GCC toolchain and TinyUSB remain pinned in the
PlatformIO files. The older fixed-resolution environments live in the checkpoint.

The ESP32 firmware supports all SDK JPEG geometries through
1600x1200 and the controls below. Later Pico firmware can adjust its menus,
requested rates and default settings within this contract.

1. Update Pico first: enter BOOTSEL (hold BOOT, tap RESET, release BOOT), then run
   `pio run -d pico -e zero_camera -t upload`. The new Pico reads both the earlier
   runtime headers and the new diagnostic extension; the old Pico rejects the
   new extension.
2. Disconnect Pico READY from ESP GPIO3. Leave the new GP7-to-GPIO2 wire
   disconnected during flashing: ESP GPIO2 must be low or floating to enter its
   serial bootloader. Connect the serial programmer, put GPIO0 to GND and reset.
3. Upload with `pio run -e usb_camera -t upload --upload-port <ESP serial port>`.
4. Disconnect programmer TX from GPIO3, free GPIO0 from GND, restore READY and
   connect the new return wire. Reset the ESP32 into normal operation. Programmer
   RX may remain connected to ESP GPIO1 to read the transmit-only startup log.

The updated Pico firmware also accepts `bootsel` over its USB serial interface.
That disconnects USB and enters the ROM bootloader, allowing subsequent Pico
uploads without pressing buttons. Physical BOOTSEL remains available if USB is
unresponsive. This command does not put the ESP32 into programming mode.

Pico UF2 is `pico/.pio/build/zero_camera/firmware.uf2`; ESP binary is
`.pio/build/usb_camera/firmware.bin`. Use PlatformIO for ESP upload so bootloader,
partitions and application receive the correct offsets. Pico updates may be
repeated independently. Bug fixes or new capabilities outside the installed
ESP contract can still require a future ESP upload.

## OBS resolution and FPS choices

Select **MJPEG Video** / **ESP32 POV Camera** as the video device. Where the host
application offers it, use custom resolution/FPS rather than a fixed preset.
Changing a mode briefly pauses capture while the real sensor is reconfigured.
Use one capture application at a time when validating modes. In the hardware
tests, OBS changed the format while FFmpeg was capturing, causing mismatched
frame sizes and macOS device-configuration lock errors. Closing OBS allowed all
ten repeated open/change/close tests to pass without a firmware change.

| Resolution | Available requested FPS |
|---|---|
| 160x120 | 30, 25, 20, 15, 10, 5 |
| 320x240 | 30, 25, 20, 15, 10, 5 |
| 480x320 | 30, 25, 20, 15, 10, 5 |
| 640x480 (default) | 30, 25, 20, 15, 10, 5 |
| 800x600 | 30, 25, 20, 15, 10, 5 |
| 1024x768 | 15, 10, 5 |
| 1280x720 | 15, 10, 5 |
| 1280x1024 | 15, 10, 5 |
| 1600x1200 | 15, 10, 5 |

These rates are requests, not guaranteed measured output. Sensor exposure,
scene detail, JPEG compression, SPI and full-speed USB constrain delivery.
The `zero_camera_fast` experiment additionally offers 60 FPS at 160x120 and
320x240, without changing ESP firmware. Do not treat it as a validated 60 FPS
camera. The standard build omits that experimental option; the diagnostic build
also includes it. See the hardware measurements below for delivered rates.

For the tested OV2640, these are practical starting points. Measurements used
requested XCLK 24 MHz, selected with `set xclk_mhz 24` on Pico CDC. That setting
is held in RAM; a Pico power cycle restores the 20 MHz startup default.

| Use | OBS custom resolution / requested FPS | Measured decoded FPS |
|---|---|---:|
| General POV / motion | 640x480 / 30 | about 28.5 |
| More detail with near-30-FPS motion | 800x600 / 30 | about 27 |
| Fastest motion, less detail | 320x240 / 60 (experimental menu) | about 57 |
| Higher resolution, slower motion | 1280x720 / 15 | about 13 |
| Full OV2640 resolution, slower video | 1600x1200 / 10 | about 10; see the repair measurements below |

The UXGA repair below removes a reproduced size-budget freeze at requested
15 FPS. That request is still not a guarantee of 15 delivered frames per second.
These bench results do not certify a moving POV scene or constant FPS in all lighting.

JPEG data stays compressed from the sensor to USB. Three fixed 64 KiB buffers
bound Pico RAM. The selected interval also sets a conservative 720 KiB/s frame
budget, except UXGA, which always uses the full 64 KiB slot. If a frame exceeds
its budget, it is counted and compression increases
(up to quality value 50). Smaller quality numbers mean better JPEG quality.
Changing modes resets to that mode's default unless an explicit quality override
is active. The firmware never upscales or repeats a frame to inflate FPS.
For UXGA, the frame-size limit was previously 49,151 bytes at requested 15 FPS;
it is now 65,536 bytes at all advertised rates. If even quality 50 produces a
larger JPEG, that frame is discarded.
Consecutive oversized frames can therefore freeze the displayed image while
the camera and SPI status channel remain healthy. This condition was reproduced
with the old 1600x1200/15 limit; a zero `config_error` alone does not prove video
is flowing. The larger allowance removes that failure in the tested scene,
but does not guarantee zero drops or permit JPEGs above 64 KiB. Monitor actual
`rx_frames`, `usb_frames` and `oversized` counters.

## Pico-controlled settings

Open the Pico's **Camera Diagnostics** CDC serial port at 115200 baud. Enter a
command followed by a newline. These controls work through the Pico; they do not
require ESP programming mode. Resolution and FPS are owned by the UVC application.

```text
status
set quality 12
set brightness 1
set hmirror 1
set aec 0
set aec_value 300
set brightness default
defaults
```

`default` clears an individual optional setting. `defaults` restores driver
settings and the selected mode's JPEG quality, preserving the host's selected
resolution/FPS. Commands are validated before changing desired state. Settings
are held in RAM and reset on Pico reboot. Edit the Pico's defaults if persistence
across power cycles is needed; ESP firmware stays the same.

| Setting | Accepted values |
|---|---|
| `quality` | 0–50 (lower is better; actual compression may rise to meet budget) |
| `xclk_mhz` | 10, 16, 20, 24; default 20; live clock update; see measured 24 MHz results below |
| `brightness`, `contrast`, `saturation`, `sharpness`, `ae_level` | -2–2 |
| `denoise` | 0–8, only if supported by the attached sensor |
| `hmirror`, `vflip`, `aec`, `aec2`, `agc`, `awb`, `awb_gain` | 0 or 1 |
| `aec_value` | 0–1200; use manual exposure (`aec=0`) |
| `agc_gain` | 0–30; use manual gain (`agc=0`) |
| `gainceiling` | 0–6 |
| `wb_mode` | 0–4 |
| `special_effect` | 0–6 |
| `bpc`, `wpc`, `raw_gma`, `lenc`, `dcw`, `colorbar` | 0 or 1 |

Not every sensor implements every setting. A failed callback pauses images,
reports `config_error`, and leaves the command link active. Send `defaults` or
correct the setting to recover. The firmware does not claim partially applied
settings succeeded. Configuration is confirmed only after a JPEG of the requested
geometry is received. Controls are based on Espressif's sensor callbacks, as in
[CameraWebServer](https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP32/examples/Camera/CameraWebServer/app_httpd.cpp).
They are not exposed in every application's native exposure dialog in this version.

The revised ESP controller allocates UXGA capture buffers once. Resolution and
quality changes use sensor callbacks while the camera peripheral remains running.
Repeated identical settings and FPS-only changes do not restart the sensor.
Changing optional controls (including restoring `default`) or recovering from a
failed operation resets the sensor's registers and reapplies the requested state,
without freeing the camera driver. Sensor changes discard three frames before
JPEG validation; no stale configuration is acknowledged as applied.

## Diagnostics and verification

One JSON report per second includes:

- `width`, `height`, `target_fps`: host-requested mode; `rx_fps_x10` and
  `usb_fps_x10`: actual receive/USB-completion rates multiplied by ten.
- `desired_generation`, `desired_token`: current requested configuration;
  `applied_generation`, `applied_token`: confirmed ESP configuration. Both pairs
  must match, and `config_error` must be zero, to confirm current settings.
- `attempted_generation`, `config_error`: most recently attempted settings and
  their failure. Error 1 invalid configuration, 2 initialization, 3 unsupported
  sensor, 4 geometry setup, 5 JPEG quality, 6 capture timeout or repeated bad JPEG
  geometry, 7 sensor reset/default restoration, 8 clock change;
  32 onward identifies the setting index in `configuration.h`.
- `sensor_pid`: actual detected sensor (38/0x26 is OV2640, 13920/0x3660 is OV3660).
- `invalid`, `spi_timeouts`, `header_errors`, `length_errors`, `payload_errors`,
  `overflows`, `rx_bytes`, `rx_prefix`: transport integrity and packet diagnostics.
- `spi_gpio`: passive Pico input-level snapshot, retaining bits 0/1/2/3/7 for
  GP0 data, GP1 chip select, GP2 clock, GP3 READY and GP7 return data. It changes
  no pin settings and is not a logic-analyzer trace. A stalled receiver with
  GP1 continuously low waits for chip select to return high before rearming.
- `capture_errors`, `oversized`, `command_errors`, `link_timeouts`, `quality`:
  latest ESP counters and current compression, including status-only replies.
- `camera_starts`, `sensor_resets`, `xclk_hz`: full driver initialization attempts,
  sensor-only reset attempts, and LEDC-reported clock frequency. A failed clock
  change reports zero until recovered. Older ESP firmware reports these as zero;
  upload the revised Pico decoder before the revised ESP firmware.
- `control_prefix`, `control_crc_received`, `control_crc_calculated`: bytes and
  CRCs actually received by ESP32 on its return input, reported on the next
  forward packet. `control_spi_hz` identifies the header exchange clock.
  `control_error`: 0 accepted, 1 missing/wrong buffer length, 2 magic, 3 version,
  4 encoded length, 5 checksum, 6 reserved bytes, 7 invalid setting values.
  These fields remain zero with the earlier ESP image. A valid default prefix
  begins `4346473202008000` (`CFG2`, version 2, length 128); a repeated `a5`
  pattern identifies the prefilled receive buffer. These are configuration
  diagnostics, separate from `config_error` during sensor setup.
- `stale_frames`, `replaced`, `seq_gaps`: discarded old configurations, frames
  replaced for bounded latency, and sender application sequence gaps. Warmup
  frames and oversize captures can produce gaps; this does not count every
  internal sensor-driver discard.
- `submit_errors`, `cancelled`, `usb_restarts`: USB submission, app-close
  cancellation and recovery reconnects. Closing a host can cancel an in-flight
  frame. `max_queue_us` measures only receive-to-submit delay, not total latency.
- `usb_gap_us`: largest interval between successful USB frame completions since
  the previous report, in microseconds. Reset on COMMIT/recovery; reports retain
  the preceding completion timestamp. An unfinished stall is measured only when
  the next frame completes. Compare it with host decode gaps to locate delays.

Host tests exercise configuration corruption/bounds, sensor callback failures,
reset recovery, mode fencing, descriptor intervals, the actual pinned TinyUSB
probe/commit implementation, DMA accounting and USB endpoint recovery.

```sh
sh test/run-usb-cam-tests.sh
sh test/run-servo-tests.sh
pio run -e usb_camera -e esp32cam
pio run -d pico -e zero_camera -e pico_camera -e zero_camera_fast -e zero_camera_uart
```

Builds/tests passed during development on 2026-11-05. Pico uses about 208 KiB of
static RAM; that does not measure runtime stack headroom. The earlier measurements
used ESP diagnostic commit `d776503`. Moving the return wire from GP6
to the intended GP7 resolved configuration reception: CRCs/tokens match and the
OV2640 sensor is detected. The SD socket is empty. The detailed investigation and
upload records are in the implementation plan.

Short macOS USB decode measurements with XCLK 20 MHz:

| Resolution | Requested rate | Approximate measured steady USB FPS |
|---|---:|---:|
| 160x120 | 30 | 30 |
| 320x240 | 60 (experimental menu) | 50 |
| 480x320, 640x480, 800x600 | 30 | 25 |
| 1024x768, 1280x720, 1280x1024, 1600x1200 | 15 | 12.5 |

**The older ESP firmware decoded all nine sizes but had intermittent failures
during repeated mode switching.** Sensor initialization sometimes failed with error 2. Pico commit
`5976ace` adds two bounded retries after a fresh, matching error and 250 ms pause;
`config_retries` counts retries and `config_retry_attempts` shows the current
request's used budget. Manual generation-only retries recovered ten VGA trials,
but repeated multi-mode changes later exhausted the budget and stopped capture.
The temporary UART connection subsequently captured an SCCB write timeout
(`ret:263`, `ESP_ERR_TIMEOUT`) during restart, followed by camera probe failure.
Another restart reported successful configuration but never returned a frame.
These observations identify failures in the full restart path; the underlying
hardware/SDK cause is not proven. These short measurements are not a stability
or zero-drop guarantee.

The current source keeps the driver initialized across configuration changes and
reports capture timeouts promptly. Recovery now covers errors 2, 6, 7 and 8 with
the same two-attempt limit; unsupported controls are not retried. Host tests and
both ESP/four Pico builds pass. **The ESP runs lifecycle revision `4735f12`.**
Pico revision `e7ea38d` added passive SPI pin levels; the subsequent UXGA and bulk
termination repair is recorded below. The uploads were verified. The attached
sensor for these checks was OV2640; the later OV3660 check is recorded below.
Remaking IO13->GP0, IO15->GP1 and IO14->GP2 restored communication after reassembly.
Post-update tests decoded all nine sizes at both 20 and 24 MHz. Ten additional
large/small resolution changes passed with OBS closed. The driver stayed
initialized once throughout; no capture errors or configuration retries occurred.
The 20 MHz startup default is retained; 24 MHz can be selected through Pico CDC
without reflashing the ESP. The first OV3660 device-side measurement appears below.

The optional `zero_camera_uart` diagnostic build reads ESP **U0T/GPIO1 -> Pico
GP5** at 115200 baud and forwards `ESP:` lines over USB CDC. Connect with power
off and retain the existing common ground and SPI wires. Open the serial monitor
before reproducing the failure; output is bounded and can be dropped while CDC
is closed/full. Normal builds leave GP5 unassigned. The diagnostic is uploaded and
Kate connected the temporary log wire; it captured the errors described above.

### OV2640 hardware recheck — 2026-11-06

Tested the connected OV2640 (PID 0x26) using the existing ESP `d776503` and Pico
`4735f12` firmware at requested XCLK 20 MHz. No firmware was changed for this
check. All nine USB modes decoded successfully in eight-second captures:

| Resolution | Requested FPS | Approximate steady USB FPS |
|---|---:|---:|
| 160x120 | 60 | 50 |
| 320x240 | 60 | 50 |
| 480x320 | 30 | 25 |
| 640x480 | 30 | 25 |
| 800x600 | 30 | 25 |
| 1024x768 | 15 | 12.5 |
| 1280x720 | 15 | 12.5 |
| 1280x1024 | 15 | 12.5 |
| 1600x1200 | 15 | 12.5 |

Nineteen further mode switches, each with a five-second decode, all passed.
The initialization failure did not recur in this run: no configuration retries,
invalid SPI packets or new capture errors. The cumulative capture-error counter
was already 14 before testing and remained 14. This does not establish that the
previously observed intermittent restart failure is fixed.

Oversized JPEGs were discarded while compression adapted during mode changes
(112 in total across this check). UXGA reached quality parameter 50, the strongest
compression allowed by this firmware. Subsequent 30-second UXGA and VGA decodes
produced 372 and 736 frames respectively; their active-stream diagnostic samples
showed no further oversized discards, capture errors or invalid packets. These
are short decode checks in the current scene, not a moving-scene image-quality,
long-duration stability, end-to-end latency or zero-drop certification.

Finished at VGA, quality parameter 16, desired/applied generation 30. Logs under
`.pio/usb-cam-research/`: `ov2640-verification-2026-11-06.log`,
`ov2640-switching-2026-11-06.log`, `ov2640-steady-2026-11-06.log`.
Only diagnostics were saved; decoded video was discarded.

### Persistent-driver and high-resolution checks — 2026-11-06

With ESP `4735f12` and Pico `e7ea38d`, all nine eight-second mode checks passed
at 20 MHz (1,775 decoded frames), then at requested 24 MHz (2,034 frames).
LEDC reports 23,981,264 Hz for the latter. Ten further four-second large/small
mode changes passed after closing OBS. Three no-op quality commands did not
reset the sensor; brightness override/default performed two sensor-only resets.
`camera_starts` remained 1 and `capture_errors` remained 0 throughout.

Kate subsequently reported pauses at 1600x1200. Longer tests reproduced a
complete lack of video despite successful configuration and a responsive status
channel. At 24 MHz, over 119 seconds between diagnostic samples, `oversized`
increased by 1,753 while both received and USB frame counters stayed fixed.
At 20 MHz, a separate test discarded 1,113 frames with the same symptom.
Neither interval added capture errors, invalid packets, SPI timeouts or USB
restarts. The source confirms that these frames are deliberately rejected by
the 48 KiB per-frame budget at requested 15 FPS after compression reaches 50.
This is a demonstrated firmware size-limit failure, not evidence that the
sensor or ESP has stopped running. It is also independent of the host conflict
seen earlier while OBS and FFmpeg were both controlling the device.

Switching to 10 FPS raises the allowed JPEG size to 64 KiB. The first 90-second
UXGA test then delivered 873 decoded frames, about 9.68 FPS after startup, at
quality 28. No further oversized frames were discarded after adaptation.
Decoded-frame arrival gaps nevertheless reached 369 ms; the corresponding
presentation-timestamp gap reached 401 ms. A successful decode does not mean
perfect pacing. Device counters averaged 9.97 FPS in that steady interval,
indicating additional losses or delays between USB completion and the decoder.
The exact cause of each short gap was not isolated.

Initial sustained tests used FFmpeg's AVFoundation input with UYVY output:

| Resolution / requested FPS | Duration | Decoded frames | Steady decoded FPS | Largest steady arrival gap |
|---|---:|---:|---:|---:|
| 1600x1200 / 10 | 90 s | 873 | 9.68 | 369 ms |
| 1280x1024 / 15 | 90 s | 1,108 | 12.33 | 205 ms |
| 1280x720 / 15 | 90 s | 1,185 | 13.17 | 240 ms |

Repeating with NV12, the pixel format observed in OBS's log, produced these
results at requested 24 MHz. These are FFmpeg/AVFoundation measurements, not
measurements taken inside OBS itself:

| Resolution / requested FPS | Duration | Decoded frames | Steady decoded FPS | Largest steady arrival gap |
|---|---:|---:|---:|---:|
| 1600x1200 / 10 | 90 s | 884 | 9.80 | 289 ms |
| 640x480 / 30 | 90 s | 2,567 | 28.47 | 116 ms |
| 320x240 / 60 | 90 s | 5,105 | 56.70 | 47 ms |
| 800x600 / 25 | 90 s | 1,753 | 19.45 | 144 ms |
| 800x600 / 30 | 60 s | 1,627 | 27.10 | 127 ms |
| 800x600 / 30 | 300 s | 8,173 | 27.24 | 132 ms |

Only the UXGA NV12 test had steady gaps over 250 ms (four, counting either
arrival or presentation-timestamp gaps); none had an arrival gap over 500 ms.
Every steady interval in this table had zero increments in oversized frames,
capture errors, invalid packets, SPI timeouts, USB restarts, sequence gaps and
replaced frames. Short host-visible gaps remain, so a clean device counter is
not sufficient to claim perfect end-to-end pacing.

The UYVY steady windows also had no increments in those counters.
These observations exclude startup/mode transitions: the cumulative
invalid counter rose from 1 to 8 across the broader session, with six header
errors and two length errors. There were also three receive timeouts and USB
recovery events outside these steady windows. Their individual causes were not
isolated, so this is not a zero-error transport claim.

The five-minute SVGA run stayed at quality 20 and decoded continuously. Its
largest steady presentation-timestamp gap was 161 ms; there were no steady
arrival or timestamp gaps over 250 ms. All the listed error/discard counters
also stayed unchanged from the first diagnostic sample to the last, including
the startup portion of this run. Sensor initialization remained at one driver
start and two earlier, deliberate sensor resets.

At 800x600, requesting 30 FPS gave better measured throughput than requesting
25 in this scene. The tighter 30-FPS size budget raised adaptive quality from
14 to 20 (stronger compression), reducing the delivered JPEG size from about
28.5 kB to 23.2 kB. Requested rate, compression and actual throughput interact;
do not assume that lowering the requested rate always improves smoothness.

The measurement tool records decoded-frame arrival times, presentation
timestamps and one-second firmware counters; it excludes the first three
seconds of decoded timestamps from steady statistics. Video and audio are not
saved. Reproducible script and detailed JSON records are in
`~/Desktop/Codex-projects/esp-cam-validation/`. Earlier mode-sweep logs remain
under `.pio/usb-cam-research/`.

The saved script includes uv dependency metadata. To reproduce a diagnostics-only
test with OBS closed, run:

```sh
uv run ~/Desktop/Codex-projects/esp-cam-validation/measure_stream.py \
  --seconds 90 --clock 24 --pixel-format nv12 800x600:30
```

Remaining wearable acceptance includes a 30-minute moving-scene soak, testing
expected lighting/power conditions and measuring end-to-end latency with a
filmed stopwatch. Inter-frame gaps measured here are not latency measurements.
No captured audio or footage is needed for the automated decode checks.

### UXGA and USB termination repair — 2026-11-06

Two separate faults were isolated:

1. The requested 15-FPS preset restricted UXGA JPEGs to 49,151 bytes. In the
   failing scene, even maximum permitted compression could not meet it, so the
   ESP discarded every frame. The Pico now requests the existing full 65,536-byte
   capacity for UXGA at every rate. This uses the installed ESP contract; the
   ESP firmware was not changed or reflashed. The UXGA descriptor's maximum
   bitrate now covers that larger allowance.
2. The pinned TinyUSB driver omitted the terminating zero-length packet when a
   short final UVC bulk payload ended on a 64-byte USB packet boundary. Its frame
   completion counter advanced while the host could wait for or combine the next
   payload. New timing diagnostics showed approximately 100 ms Pico completion
   gaps during approximately 300 ms host decode gaps. The adapter now submits
   the required ZLP and retains frame ownership until completion, including
   safe cancellation and failure handling. This follows the
   [UVC bulk transport discussion](https://www.mail-archive.com/linux-uvc-devel%40lists.berlios.de/msg03790.html)
   and [documented bulk termination behavior](https://doc.segger.com/UM09001_emUSBD.html#USBD_BULK_WriteEx()).

The frame-budget repair alone restored UXGA/15 (~9.80 FPS over 90 seconds), but
five gaps above 250 ms remained. After the USB termination repair, both 90-second
checks below had no steady arrival or timestamp gaps above 250 ms:

| Requested mode | Decoded frames / duration | Steady FPS | Maximum arrival gap |
|---|---:|---:|---:|
| 1600x1200 / 10 | 898 / 90 s | 9.98 | 203 ms |
| 1600x1200 / 15 | 900 / 90 s | 9.99 | 189 ms |

These used NV12 through FFmpeg/AVFoundation at requested XCLK 24 MHz. The first
run discarded one oversized frame in the steady window while quality adapted
from 20 to 22. The second recorded one invalid SPI header and one receive
timeout, recovered automatically, and continued decoding. Neither run added a
capture error or USB restart. Thus the reproduced long freeze and recurring
300–400 ms host gaps were removed in these tests, but isolated frame losses
remain possible; 15 requested FPS still delivered about 10 in this scene.

The uploaded `zero_camera_uart` UF2 SHA-256 is
`e391baa957b70ade2ed0538f8d8fd1eab96b8f6560ebc06b792b2a968cf37118`.
USB/servo host suites, all four Pico builds and the ESP compatibility build
passed. The SDK files and wiring are unchanged. Test logs and timestamped JSON
files are under `~/Desktop/Codex-projects/esp-cam-validation/`; the initial
post-repair records are `20261106-031450-1600x1200-10.json` and
`20261106-031621-1600x1200-15.json`.

A first five-minute attempt is **not a completed soak pass**. It decoded 2,569
UXGA frames over about 257 seconds before a second host client changed the
device to 1280x720. macOS `UVCAssistant` logged the format change and an added
streaming client at 03:20:52; Pico confirmed the new mode with no USB restart.
FFmpeg still expected 1600x1200 raw frames and rejected the smaller buffers.
This is separate from the firmware faults above. The source application was
not identified. Evidence: `20261106-032205-1600x1200-15.json` and
`uxga-soak-host-format-change.log`. The measurement tool now aborts explicitly
on unexpected mode changes or ten seconds without a decoded frame and reports
the unfinished trailing gap, rather than relying only on completed gap pairs.

At the unchanged 20 MHz startup clock, a subsequent 60-second UXGA/15 check
decoded 572 frames (~9.54 FPS), with a maximum 180 ms arrival gap and no steady
increments in oversized/capture/invalid/SPI-timeout/USB-restart counters.
Record: `20261106-032531-1600x1200-15.json`.

The planned repeat soak and smaller-mode sweep stopped before capturing because
OBS had reopened. Its current properties show 1280x720 / 15, with buffering
enabled. That preview was left available and its settings were not changed.
An uninterrupted five-minute post-repair soak and a new smaller-mode hardware
sweep remain outstanding; the pre-repair all-mode measurements above and the
post-repair host tests must not be mistaken for those hardware checks.
Final read-only diagnostics confirmed OBS's 1280x720/15 configuration applied
at 20 MHz, with 63 additional USB completions over approximately five seconds
and no new capture, oversize, invalid-packet, SPI-timeout or USB-restart errors.
This confirms ongoing device transfers, not an OBS-internal decoded FPS result.

### First OV3660 hardware check — 2026-11-06

After Kate fitted the OV3660, the installed ESP automatically detected PID
`0x3660` (13920). No code, firmware, wiring or runtime settings were changed for
this check. OBS already owned the camera at 1600x1200 / requested 15 FPS, so
only the independent CDC diagnostics were read; no second video client opened.

Over 59.002 seconds between diagnostic samples, 737 JPEG frames were received
and completed over USB: **12.49 FPS** at XCLK 20 MHz, JPEG quality parameter 28.
The maximum measured USB-completion gap was **136.025 ms**. The selected mode
remained constant, desired/applied generations matched throughout, and
`config_error` was zero. Capture errors, oversized discards, invalid packets,
SPI timeouts, USB restarts, configuration retries, camera starts and sensor
resets did not increase. Earlier cumulative counters already included seven
oversized frames, one invalid packet and two USB restarts; this is not a
zero-error claim for the period before measurement.

These are device-side delivery measurements while OBS was active, not a host
decode/FPS/latency measurement or a resolution-switching/long-soak pass. The
existing menu still ends at 1600x1200; fitting this sensor does not add USB
modes automatically. Evidence: `20261106-033606-ov3660-live-obs.json` under
`~/Desktop/Codex-projects/esp-cam-validation/`. OBS was left in control.

### OV3660 higher-rate feasibility — 2026-11-06

The [OmniVision datasheet, table 2-2](https://files.seeedstudio.com/wiki/SeeedStudio-XIAO-ESP32S3/res/OV3660_datasheet.pdf#page=29)
does list 3MP/15, 1080p/20, 720p/45, XGA/45, VGA/60 and QVGA/120. These sensor
transfer rates use specified clocks and cropping/subsampling modes. They are
not delivered USB-camera rates for this assembly. In
[Espressif issue 232](https://github.com/espressif/esp32-camera/issues/232), the
maintainer identifies ESP XCLK/PCLK capture limits; the original reporter saw
lower OV3660 throughput than OV2640 despite three buffers. Later experimental
PLL settings reportedly improved FPS by about 50%, without establishing a
measured 120-FPS mode. This is useful prior work, not a validated preset for
our current hardware/driver.

Our firmware already disables Wi-Fi/Bluetooth, captures sensor-compressed JPEG,
and uses two PSRAM buffers with `CAMERA_GRAB_LATEST`. Espressif documents that
[two or more buffers enable continuous capture](https://github.com/espressif/esp32-camera#important-to-remember);
three buffers are not an automatic high-FPS switch. The driver also programs
[mode-dependent PLL, binning and frame timing](https://github.com/espressif/esp32-camera/blob/f0bb429/sensors/ov3660.c).
The application's requested interval currently paces delivery; it is not an
arbitrary sensor PLL or readout-rate command.

The wired link has finite limits: JPEG SPI is 8 Mbit/s (at most 1,000,000 B/s
before headers/gaps), while [RP2040 USB is full speed, 12 Mbit/s](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf#page=384).
The usual 720 KiB/s firmware budget corresponds to about 24 KiB/frame at 30 FPS,
16 KiB at 45, 12 KiB at 60 and 6 KiB at 120. These are arithmetic payload
allowances, not measured modes. UXGA has its separate 64 KiB allowance. A
50,000-byte JPEG at 45 FPS would require 2.25 MB/s before overhead, beyond the
current SPI link. More cores or removing Wi-Fi cannot change that wire rate.

Current software boundaries and next experiments:

- VGA is advertised through 30 FPS; 720p/XGA through 15. Pico-only descriptor
  changes can request higher rates up to the existing 60-FPS protocol limit,
  but must be validated against actual sensor delivery and JPEG size.
- The existing experimental QVGA/60 choice can be benchmarked without flashing.
  VGA/60 and 720p/30 are investigation targets, not promised performance.
- The installed ESP accepts geometries only through 1600x1200, intervals no
  faster than 60 FPS, and JPEGs no larger than 64 KiB. 1080p, full 3MP, 120 FPS
  requests and custom PLL/readout profiles require ESP capability changes as
  well as Pico updates. They cannot be enabled solely by adding OBS choices.
- If those capabilities are pursued, prepare one reviewed ESP update exposing
  bounded sensor-specific timing profiles to the Pico. Subsequent profile
  tuning should stay on the Pico; verify image geometry, exposure/FOV changes,
  corruption, gaps and sustained host decode before promoting any new mode.

Live clock probes at 1600x1200/requested 15 FPS, with OBS still in control:

| Trial | XCLK | USB-completed FPS | JPEG quality values | Mean JPEG bytes |
|---|---:|---:|---|---:|
| Initial baseline | 20 MHz | 13.80 | 28 | 48,705 |
| Initial clock change | 24 MHz | 11.07 | 16 | 59,349 |
| Repeat baseline | 20 MHz | 9.32 | 16 | 60,016 |
| Repeat clock change | 24 MHz | 10.97 | 16–18 | 59,216 |

Clock changes reapply mode defaults, resetting adaptive JPEG quality. Therefore
the first comparison is confounded by compression, and the repeat also adapted
once; neither establishes a stable clock-only speedup or maximum sensor FPS.
The repeat at 24 MHz discarded one oversized frame. No trial added capture
errors, invalid SPI packets, receive timeouts or USB restarts. Original 20 MHz
was restored; adaptive quality history was reset by the clock changes. No
firmware upload or OBS mode change was made. Evidence in the Desktop validation
directory: `20261106-034131-ov3660-clock-comparison.json` and
`20261106-034357-ov3660-clock-repeat.json`. A controlled resolution/FPS sweep
still requires the test application to have sole camera ownership.

A final comparison held JPEG quality **28** explicitly at both clocks:

| XCLK | Measured interval | USB-completed FPS | Mean JPEG bytes | Maximum completion gap |
|---|---:|---:|---:|---:|
| 20 MHz | 30.999 s | 12.55 | 50,186 | 135.307 ms |
| 24 MHz | 30.001 s | 11.13 | 50,392 | 119.560 ms |

Quality stayed at 28 throughout both windows. Capture, oversize, invalid,
SPI-timeout and USB-restart counter deltas were all zero. This short comparison
does not establish maximum performance or control lighting/exposure, but gives
no basis to promote 24 MHz as faster for this OV3660 mode. It also does not
identify which capture stage explains the lower rate. Restored **20 MHz and
left an explicit quality-28 override** to retain the pre-test compression and
full-resolution behavior. Unlike the earlier adaptive state, this quality
request will persist across mode changes until `defaults` or a Pico reboot;
`defaults` also resets the other optional controls. Firmware and OBS mode remain
unchanged. Evidence: `20261106-034858-ov3660-fixed-quality-clock.json`.

## Source and design

- [Approved runtime design](docs/usb-camera-runtime-design.md) and
  [implementation plan](docs/usb-camera-runtime-plan.md).
- Shared configuration, modes, protocol and buffer ownership: `shared/usb_cam/`.
- ESP camera control and SPI master: `src/usb_cam/`.
- Pico SPI DMA, UVC/CDC and pinned TinyUSB adapters: `pico/src/`.
- Prior art: [GB Interceptor](https://github.com/Staacks/gbinterceptor),
  [TinyUSB](https://github.com/hathach/tinyusb),
  [Espressif camera](https://github.com/espressif/esp32-camera), and
  [ArduCAM PICO_SPI_CAM](https://github.com/ArduCAM/PICO_SPI_CAM).
  No exact previously validated ESP32-to-RP2040 UVC bridge was established.
- Hardware constraints: [RP2040 datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)
  and [ESP boot pins](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/boot-mode-selection.html).

The SDK is not edited. Version-guarded project adapters retain buffer-ownership
and safe endpoint-reopen fixes, supply complete multi-mode negotiation and clamp
MJPEG limits to real buffers. The video adapter also terminates short bulk
payloads that end on a full USB packet with a zero-length packet before releasing
frame ownership. USB VID/PID CAFE:4050 is a prototype identifier,
not an assigned product identity. See [third-party notices](pico/THIRD_PARTY_NOTICES.md).

## Automatic sensor profiles (prepared firmware, 2026-11-06)

The upgraded pair detects the sensor after power-on and selects its USB menu.
Changing modules requires power off, but no sensor-specific firmware build. The
Pico initially exposes **ESP32 Camera Discovery** (CDC diagnostics only). Two
fresh validated sensor reports select the camera identity; expect one brief USB
reconnection. With no supported sensor, diagnostics remain available.

| Detected sensor / ESP firmware | USB product / prototype PID | Menu |
| --- | --- | --- |
| OV2640, either firmware | ESP32 OV2640 USB Camera / CAFE:4051 | Existing nine sizes, through 1600×1200 |
| OV3660, old ESP firmware | ESP32 OV3660 Legacy Camera / CAFE:4052 | Existing nine sizes; no PLL overrides |
| OV3660, upgraded ESP firmware | ESP32 OV3660 USB Camera / CAFE:4053 | Eleven sizes, including 1920×1080 and 2048×1536 |
| Not yet detected / unsupported | ESP32 Camera Discovery / CAFE:4050 | Diagnostics only |

Choose the newly named device in OBS after the first upgrade. UVC frame indices
1–9 retain their existing geometry. FHD requests 5/10 FPS (default 10), QXGA
requests 5/10 FPS (default 5). Both use the full 64 KiB JPEG slot; starting quality
is 28/32 respectively and compression increases if necessary. **These additional
modes and rates are candidates pending hardware validation, not measured output.**
An unusually detailed frame may exceed 64 KiB even at maximum compression.

The standard Pico builds retain the previous rates for existing sizes. The
`zero_camera_fast` and `zero_camera_uart` measurement builds additionally expose
60 FPS VGA and 30 FPS HD/XGA on upgraded OV3660; their existing QVGA/QQVGA 60 FPS
requests remain. Do not equate an OBS selection with an achievable frame rate.
The same menu table drives descriptors, UVC validation and the ESP request.

### OV3660 timing experiments without another ESP upload

The upgrade exposes checked PLL controls through Pico CDC, after sensor detection.
XCLK remains 20 MHz and PLL multiplier 0 means the SDK's normal geometry-specific
timing. OV2640 retains its original sensor timing and rejects OV3660 PLL commands.
A useful baseline for a non-QXGA mode at XCLK 20 MHz is:

```text
set pll_multiplier 30
set pclk_div 12
status
set pll_multiplier 0
```

The first command explicitly selects the usual 50 MHz SYSCLK / 10 MHz PCLK
calculation; the second lowers PCLK to about 8.33 MHz while retaining SYSCLK. It
is a capture-throughput experiment, not a measured improvement. Multiplier 0
restores the SDK timing, including its separate QXGA recipe. Available integer
controls: `pll_multiplier`, `pll_sys_div`, `pll_pre_div`, `pll_root_2x`,
`pll_seld5`, `pclk_div`. Field ranges and computed clock combinations are checked:
VCO 150–500 MHz, SYSCLK <=54 MHz and PCLK <=10 MHz. Invalid combinations are
rejected atomically; change one parameter at a time and check applied generation,
configuration error and real frame gaps. `defaults` clears all overrides, while
OBS resolution changes preserve explicit timing and other settings. Settings
remain in RAM. PLL callback failure stops publication until a valid new request.

Timing overrides run **after** the driver's geometry setter; switching back to
SDK timing also reprograms geometry. No arbitrary register writes or speculative
crop settings are exposed. The [Espressif OV3660 driver](https://github.com/espressif/esp32-camera/blob/f0bb429/sensors/ov3660.c)
and [FPS discussion](https://github.com/espressif/esp32-camera/issues/232) explain
why geometry and clock changes interact. Bounds use the
[OV3660 datasheet](https://files.seeedstudio.com/wiki/SeeedStudio-XIAO-ESP32S3/res/OV3660_datasheet.pdf),
not unverified performance claims from the discussion.

### Upgrade order and compatibility

Upload the new Pico first; it understands old ESP status packets and deliberately
withholds new modes until capability bit 0 arrives. Then upload the reviewed ESP
USB firmware once. Default configuration packets remain byte-identical to v2.
Custom PLL fields occupy bytes 76–81; header capabilities occupy bytes 116–119.
Old Pico firmware rejects the extended header, so upgrading ESP alone is not a
working deployment. The fixed 128-byte packets, CRC, wiring and SPI speeds remain.
ESP initializes buffers for QXGA, clamped by the SDK to the detected sensor's
maximum before allocation, and retains them across mode changes.

This release adds the controls needed for measured OV3660 optimization; it does
not yet establish a faster sensor profile. Preserve the previous measured clock
results above when comparing the new firmware.

The upgraded wire contract accepts requested intervals up to 120 FPS so future
Pico-only tuning does not need another ESP upload. Current USB menus remain at
60 FPS or below; this protocol ceiling does not establish achievable sensor or
USB performance. Legacy ESP menus remain bounded by their existing contract.

### Build, review and staged Pico validation

Commits `6ea09b2` and `d29b670` implement this upgrade. The complete host USB
suite, servo suite, both ESP builds and four Pico builds passed. An independent
review found no blocking defects and verified the important allocation and PLL
behaviors directly in the installed ESP archive. Pico's existing linker warning
about an RWX load segment remains; memory usage is 208716/262144 bytes.

The UART Pico build was uploaded with picotool read-back verification after
saving a full flash backup. SHA256:

- Prepared ESP binary: `f6559a422c6fcab3f15647f838aa57f1a65d1c57633d1fb165e77c8f0227b0f6`
- Installed Pico UF2: `39daf8c13fd976835b402dc0bbb5e0c949ce70d975e669cf0b3132158b6b39d9`

The existing ESP reported sensor PID 13920 (OV3660), capabilities 0. The Pico
automatically selected profile 2 and macOS eventually enabled **OV3660 Legacy
MJPEG Video**, CAFE:4052. Initially macOS enumerated the new identity but did not
register drivers; its logs showed accessory authorization activity, after which
both UVC and CDC appeared. No firmware patch was needed for that host delay.
A new identity may prompt the Mac to allow the accessory; approve the specific
camera if asked, without changing global accessory security settings.

Three six-second FFmpeg decoding checks completed: VGA/30 (168 decoded frames),
UXGA/10 (57), VGA/30 (168). Every decoded frame had the requested dimensions.
Capture errors, invalid packets, oversized frames, SPI timeouts, USB restarts,
configuration retries and command errors had **zero new increments** during the
sequence. This is a compatibility smoke test, not a sustained FPS measurement.
Evidence: `~/Desktop/Codex-projects/esp-cam-validation/sensor-profile-legacy-smoke.json`.

**Deployment still pending:** the ESP capability upgrade has not been flashed.
FHD/QXGA and new timing controls remain intentionally unavailable on the installed
old ESP. Their image integrity and performance, plus OV2640 physical regression
testing, remain to be measured after the ESP upload and relevant sensor fitting.
The system is left on VGA with no capture test running.
