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
camera. The standard build omits that unmeasured option.

JPEG data stays compressed from the sensor to USB. Three fixed 64 KiB buffers
bound Pico RAM. The selected interval also sets a conservative 720 KiB/s frame
budget. If a frame exceeds its budget, it is counted and compression increases
(up to quality value 50). Smaller quality numbers mean better JPEG quality.
Changing modes resets to that mode's default unless an explicit quality override
is active. The firmware never upscales or repeats a frame to inflate FPS.

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
| `xclk_mhz` | 10, 16, 20; default 20, camera restarts to change it |
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

## Diagnostics and verification

One JSON report per second includes:

- `width`, `height`, `target_fps`: host-requested mode; `rx_fps_x10` and
  `usb_fps_x10`: actual receive/USB-completion rates multiplied by ten.
- `desired_generation`, `desired_token`: current requested configuration;
  `applied_generation`, `applied_token`: confirmed ESP configuration. Both pairs
  must match, and `config_error` must be zero, to confirm current settings.
- `attempted_generation`, `config_error`: most recently attempted settings and
  their failure. Error 1 invalid configuration, 2 initialization, 3 unsupported
  sensor, 4 geometry setup, 5 JPEG quality, 6 repeated bad JPEG geometry;
  32 onward identifies the setting index in `configuration.h`.
- `sensor_pid`: actual detected sensor (38/0x26 is OV2640, 13920/0x3660 is OV3660).
- `invalid`, `spi_timeouts`, `header_errors`, `length_errors`, `payload_errors`,
  `overflows`, `rx_bytes`, `rx_prefix`: transport integrity and packet diagnostics.
- `capture_errors`, `oversized`, `command_errors`, `link_timeouts`, `quality`:
  latest ESP counters and current compression, including status-only replies.
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
pio run -d pico -e zero_camera -e pico_camera -e zero_camera_fast
```

Builds/tests passed during development on 2026-11-05. The runtime Pico build uses
about 208 KiB of static RAM; that does not measure runtime stack headroom.
On 2026-11-05, runtime commit `9087b4b` was uploaded to both controllers. The
ESP32 upload completed once with flash hash verification; the Pico upload
verified its flash and restarted as VID/PID CAFE:4050. macOS AVFoundation
enumerated all nine resolutions and their discrete frame-rate choices.
After reconnection and reseating GP7-to-ESP2, forward status packets were valid
but every returned configuration was rejected. A temporary passive Pico PIO
trace captured 128 correct return bytes at GP7 with no mismatches; this does not
prove the bytes arriving at ESP32 GPIO2. Faster output edges and a one-bit-offset
experiment did not establish a fix, and those experiments were removed.
**Runtime video remains unverified and no frames are currently delivered.**
The diagnostic update at `d776503` was uploaded to both boards. The additional
ESP32 upload on 2026-11-05 completed with flash hash verification through
`/dev/cu.usbserial-140`. It includes ESP receive diagnostics and the slower
control exchange. After reassembly, ESP diagnostics reported an all-zero return
prefix and CRC at 1 MHz (`control_error=2`). Kate confirmed no SD card is inserted.
A temporary Pico test forced GP7 high and low while CS was active: Pico read
back high correctly, while ESP still received zeros in both cases. The temporary
test was removed and normal Pico firmware restored. Check return-wire continuity,
connector/pin identification and the ESP input path before further video testing.
The previous 25 FPS VGA result belongs to the checkpoint, not every new resolution.

Hardware acceptance: decode each mode, repeatedly switch large/small sizes,
change a sensor setting and restore defaults, reboot either board, close/reopen
applications and run a 30-minute moving-scene soak. Measure actual frames and
losses; use a filmed stopwatch to measure end-to-end latency. No captured audio
or footage is needed for the automated decode checks.

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
