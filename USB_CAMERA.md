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
| Full OV2640 resolution, slower video | 1600x1200 / 10 | about 9.7–9.8; brief gaps remain |

Avoid 1600x1200 / 15 for dependable continuous capture with the current firmware:
the longer tests below reproduced a frozen feed at both 20 and 24 MHz. These
bench results do not certify a moving POV scene or constant FPS in all lighting.

JPEG data stays compressed from the sensor to USB. Three fixed 64 KiB buffers
bound Pico RAM. The selected interval also sets a conservative 720 KiB/s frame
budget. If a frame exceeds its budget, it is counted and compression increases
(up to quality value 50). Smaller quality numbers mean better JPEG quality.
Changing modes resets to that mode's default unless an explicit quality override
is active. The firmware never upscales or repeats a frame to inflate FPS.
The frame-size limit is 49,151 bytes at a requested 15 FPS and 65,536 bytes at
10 FPS. If even quality 50 produces a larger JPEG, that frame is discarded.
Consecutive oversized frames can therefore freeze the displayed image while
the camera and SPI status channel remain healthy. This condition was reproduced
at 1600x1200/15; a zero `config_error` alone does not prove video is flowing.
Reducing the requested rate to 10 allows larger JPEGs but is not a zero-drop
guarantee. Monitor actual `rx_frames`, `usb_frames` and `oversized` counters.

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
both ESP/four Pico builds pass. **The ESP runs lifecycle revision `4735f12`;
the Pico runs `e7ea38d`, which also reports passive SPI pin levels.** Both uploads
were verified. Kate confirmed that the attached sensor is OV2640, not OV3660.
Remaking IO13->GP0, IO15->GP1 and IO14->GP2 restored communication after reassembly.
Post-update tests decoded all nine sizes at both 20 and 24 MHz. Ten additional
large/small resolution changes passed with OBS closed. The driver stayed
initialized once throughout; no capture errors or configuration retries occurred.
The 20 MHz startup default is retained; 24 MHz can be selected through Pico CDC
without reflashing the ESP. OV3660 still has no physical test results.

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
MJPEG limits to real buffers. USB VID/PID CAFE:4050 is a prototype identifier,
not an assigned product identity. See [third-party notices](pico/THIRD_PARTY_NOTICES.md).
