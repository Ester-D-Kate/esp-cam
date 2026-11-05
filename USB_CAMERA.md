# ESP32-CAM + RP2040-Zero USB POV camera

USB-only firmware on branch `codex/usb-cam`. This is a **hardware-test prototype**:
compilation and host tests cannot establish image quality, reliable enumeration,
latency or sustained FPS on the physical boards.

The camera sensor supplies JPEGs. ESP32 transmits them over SPI; RP2040 receives
with DMA on core 1 and runs a standard MJPEG UVC webcam plus a diagnostic serial
port on core 0. No computer-side video conversion service is required. The host
sees one composite USB device with video and diagnostics. It is not a USB disk.

This first version prioritizes live video. SD, Wi-Fi, recording button, servos
and gyro are inactive in the USB build. The previous firmware remains selectable
as the `esp32cam` environment. The USB firmware leaves GPIO12 unused; it does not
change or remove the old button/debounce implementation.

## What output to expect

Start with VGA30. Profiles describe **requested/advertised timing, not measured
FPS**. Lighting, exposure, sensor, JPEG detail, wiring and host scheduling all
affect actual throughput. Read the counters and measure actual host capture.

| Profile | ESP32 environment | Zero environment | Image | Target FPS | JPEG ceiling |
|---|---|---|---|---|---|
| 1: moving POV, default | `usb_vga30` | `zero_vga30` | 640 × 480 | 30 | 24 KiB |
| 2: more detail | `usb_svga15` | `zero_svga15` | 800 × 600 | 15 | 48 KiB |
| 3: HD experiment | `usb_hd10` | `zero_hd10` | 1280 × 720 | 10 | 60 KiB |
| 4: link bring-up/fallback | `usb_qvga30` | `zero_qvga30` | 320 × 240 | 30 | 24 KiB |

**Flash matching profiles on both boards.** A mismatch is rejected and increments
`invalid`; it does not send wrongly sized images to the host. Each profile has
a distinct USB product ID to reduce host format-cache confusion.

The RP2040 has full-speed USB (12 Mbit/s, not 480 Mbit/s). Video uses standard
UVC bulk transfers with 64-byte USB packets and a 2048-byte software staging
buffer, following GB Interceptor's transport. Bulk scheduling shares bandwidth
with other USB traffic. The chosen JPEG ceilings consume at most 0.74 MB/s at
target FPS; they do not prove the camera can produce those rates.

JPEGs are passed through without decoding, resizing or artificial sharpening.
When a scene produces a JPEG above the ceiling, that frame is counted/discarded
and compression increases by two quality steps, up to 50. Smaller JPEG quality
numbers mean better image quality. Compression does not automatically oscillate
back down; reset to restore the initial quality. HD uses a different aspect ratio
and may change the sensor's field of view. There is no stabilization, autofocus
control, audio, or HD/60 FPS capability added by this bridge. For moving POV use,
good light and a correctly focused lens remain essential.

## Wiring (power off first)

Use GPIO labels, **not physical pin numbers**. Signals are 3.3 V. Keep wires
short (start below 10 cm) and route ground beside SCK. These pins are available
on the Waveshare RP2040-Zero; GP8 through GP14 are reserved and unused by this
firmware. GP16 is its RGB LED and is deliberately unused. The SPI0 functions
follow the [RP2040 datasheet, GPIO functions table](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf).

| ESP32-CAM AI Thinker | Direction | Waveshare RP2040-Zero | Purpose |
|---|---|---|---|
| GPIO14 | → | GP2 | SPI0 SCK |
| GPIO13 | → | GP0 | SPI0 RX / ESP32 MOSI |
| GPIO15 | → | GP1 | SPI0 CS, active low |
| GPIO3 / U0R | ← | GP3 | READY / acknowledge |
| GND | ↔ | GND | Common signal reference |

There is no MISO wire. SPI is **8 MHz, mode 1, MSB first**. READY goes high only
after the Pico has armed its receive DMA. ESP32 asserts CS, waits for READY to
fall, then sends one header and JPEG while CS stays low. CRC, exact byte counts
and JPEG geometry are checked before a frame becomes eligible for USB.

Remove the microSD card and disconnect servo/gyro wiring: SPI uses GPIO13–15,
which also connect to the SD socket. GPIO0 must not be grounded during normal
ESP32 operation. Keep GPIO12 low at boot as required by the board's flash-voltage
strap; do not add an external pull-up.

For initial testing, power the Zero from the computer's USB data port and power
the ESP32-CAM through its **5V input** with a stable regulated supply. Connect
grounds. Do not power ESP32-CAM from the Zero's 3V3 output. Do not join two
independent 5V outputs or backfeed the computer. A compact single-supply build
can be designed after measuring current and checking the exact power circuit;
this configuration advertises 100 mA for the separately powered USB bridge.

**Programming conflict:** GPIO3 is the ESP32 programming UART RX and the READY
input. Disconnect Pico GP3 ↔ ESP32 GPIO3 while flashing ESP32. During normal operation,
disconnect the programmer's TX wire from GPIO3, then reconnect READY. Programmer
RX may remain connected to ESP32 GPIO1 for TX-only logs, with common ground and
3.3 V UART signaling.

## Build and flash

Open the repository in VS Code/PlatformIO for ESP32. Open the `pico` subfolder as
a second PlatformIO project for RP2040-Zero. A multi-root VS Code workspace is
provided as `usb-camera.code-workspace`.

PlatformIO is already installed on this development machine at
`/Users/kate/.platformio/penv/bin/pio`; substitute that path if `pio` is not on
PATH. On a fresh machine, `uv tool install platformio==6.2.0` installs the CLI.

From the repository root:

```sh
pio run -e usb_vga30
pio run -d pico -e zero_vga30
```

The Pico project uses a pinned community PlatformIO platform with native Pico
SDK support, rather than the official platform's Arduino-only setup. GCC 14 is
pinned to include a native Apple Silicon compiler. Dependencies are downloaded
on the first build. No installed SDK files are modified by this project.

Flash ESP32 using its USB/UART programmer (READY disconnected, GPIO0 grounded
only while entering the bootloader):

```sh
pio run -e usb_vga30 -t upload --upload-port YOUR_ESP32_SERIAL_PORT
```

Remove GPIO0–GND after flashing and reset. For the RP2040-Zero, hold BOOT while
connecting USB, or hold BOOT and tap RESET. Then upload:

```sh
pio run -d pico -e zero_vga30 -t upload
```

Alternatively, copy `pico/.pio/build/zero_vga30/firmware.uf2` onto the `RPI-RP2`
bootloader drive. The drive disappears after reboot; the device should then
enumerate as **ESP32 POV Camera** and a diagnostic serial port. Re-enter BOOTSEL
to reflash: this custom USB firmware does not implement Arduino's serial-touch
reset. An ordinary Raspberry Pi Pico can use `pico_vga30` with the same GPIO map.

## First hardware test

1. Flash matching QVGA30 profiles first if wiring is unproven; otherwise start
   with the default VGA30 pair. Reconnect signal wires with power off.
2. Connect the Zero's USB to the computer, then power ESP32. Inspect ESP32 logs
   if the camera fails to start or the Zero reports no received frames.
3. Open OBS or another UVC application. Select **ESP32 POV Camera** (shown as
   **MJPEG Video** in macOS AVFoundation/FFmpeg), MJPEG and
   the dimensions/FPS for the flashed profile. Host/app compatibility still
   needs testing on your machine; an app that insists on YUY2 is not supported.
4. Open the Zero's diagnostic serial port at 115200. It emits one JSON line per
   second. Serial output is nonblocking so a slow monitor cannot stall video.
5. Test a bright static scene, then fast hand/head movement, then indoor light.
   Run each for 60 seconds. Record actual USB FPS, source/transport losses and
   visible blur. Do not infer performance from the application's selected FPS.
6. Close/reopen the camera app, unplug/replug USB, reset ESP32, and reset Zero.
   Then perform a 30-minute moving-scene soak. Confirm recovery and no increasing
   invalid-frame count. No automatic tests in this repo replace these checks.

Diagnostic fields:

| Field | Meaning |
|---|---|
| `rx_fps_x10`, `usb_fps_x10` | Measured receive / USB completion rate, multiplied by 10 |
| `rx_frames`, `usb_frames` | Cumulative valid receives / firmware USB completions |
| `invalid`, `spi_timeouts` | Corrupt, truncated, mismatched or oversized SPI packets / incomplete transactions |
| `header_errors`, `length_errors`, `payload_errors`, `overflows` | Invalid-packet breakdown: header, total length, JPEG/CRC, receive-buffer overflow |
| `rx_bytes`, `rx_prefix` | Last SPI transaction's byte count and first 16 bytes as hexadecimal; a valid header starts `504f56310100300001000000` for profile 1 |
| `replaced` | Valid preview frames skipped to keep latency bounded, including while no app is reading |
| `seq_gaps`, `sender_resets` | Missing ESP32 application sequence numbers / inferred sequence restart |
| `submit_errors`, `cancelled`, `usb_restarts` | USB submission failures, cancelled frames, recovery reconnects |
| `jpeg_bytes`, `quality` | Last submitted frame size and ESP32 JPEG quality setting |
| `capture_errors`, `oversized`, `link_timeouts` | Last received ESP32 error counters |
| `max_queue_us` | Largest receive-to-USB-submit wait; **not** end-to-end camera latency |
| `clk_peri` | Actual Pico peripheral clock; must be at least 96 MHz for 8 MHz slave SPI |

USB completion means the controller completed sending. Check decoded host frames
too; controller counters alone do not establish successful display. ESP32 camera
driver discards before the application gets a frame are not all represented in
`seq_gaps`; the achieved FPS and visual timestamps are the relevant checks.

For true glass-to-glass latency, film a rapidly changing stopwatch/display and
the webcam preview in the same reference video. For frame loss, capture a known
changing frame counter and inspect the host recording. Good FPS alone does not
prove low latency or clear motion.

## Implementation and research

* Shared profiles, wire protocol and slot ownership: `shared/usb_cam/`.
* ESP32 capture and SPI sender: `src/usb_cam/`.
* Pico SPI DMA receiver, UVC driver adapter and diagnostics: `pico/src/`.
* Design, bandwidth calculations and primary-source links:
  [docs/usb-camera-design.md](docs/usb-camera-design.md).

Existing projects inspected at source level:

| Source | What informed this implementation | Important difference |
|---|---|---|
| [TinyUSB video_capture](https://github.com/hathach/tinyusb/tree/86ad6e56c1700e85f1c5678607a762cfe3aa2f47/examples/device/video_capture) | UVC descriptors, probe/commit, asynchronous frame transfer | Example video is generated/static; our JPEGs come from a sensor |
| [GB Interceptor](https://github.com/Staacks/gbinterceptor/tree/164127fe03bcd3b7238259187197dde7a315ec89/firmware) | RP2040 UVC, front/back buffers, completion callback and CDC | Synthetic Game Boy video and specialized JPEG encoding do not demonstrate natural-scene throughput |
| [ArduCAM PICO_SPI_CAM](https://github.com/ArduCAM/PICO_SPI_CAM/tree/378f3c045122b99625f59ad5fcb1aed057de7e53/C) | Read a whole compressed frame from SPI, then send over USB | Its Pico is SPI master and uses a proprietary CDC host app; we use SPI slave DMA and UVC |
| [Espressif esp32-camera](https://github.com/espressif/esp32-camera) | Sensor JPEG, PSRAM and continuous double buffering | It does not supply this RP2040 bridge |

No exact validated ESP32-CAM → RP2040-Zero SPI → UVC project was established by
this research. The bridge protocol is new code and needs the hardware tests
above. TinyUSB stays at the SDK's pinned 0.18.0 commit. Its internal buffer
ownership accessor is isolated in `video_driver.c` and compile-time guarded;
review it when upgrading TinyUSB. `dcd_driver.c` also adapts the upstream RP2040
ISO endpoint abort sequence to prevent a pending packet surviving stream
reactivation. Its wait is bounded; unsupported early silicon or an abort timeout
requests a full USB reconnect. The reset helper is exercised against a host
register model. See [third-party notices](pico/THIRD_PARTY_NOTICES.md) for the
upstream source and license. Physical close/reopen testing is still required.
Bulk endpoint reopening also reuses its existing DPRAM allocation and safely
aborts pending transfers. The initial isochronous build produced a macOS USB
pipe error during hardware testing; the current build uses bulk UVC instead.
The USB VID/PIDs are prototype identifiers
from the TinyUSB example convention, not an assigned product identity.

## Local verification

```sh
sh test/run-usb-cam-tests.sh
sh test/run-servo-tests.sh
pio run -e usb_vga30 -e usb_svga15 -e usb_hd10 -e usb_qvga30 -e esp32cam
pio run -d pico -e zero_vga30 -e zero_svga15 -e zero_hd10 -e zero_qvga30 -e pico_vga30
```

Host tests use AddressSanitizer/UndefinedBehaviorSanitizer and the actual USB
descriptor source. They cover CRC, corruption/truncation, JPEG bounds, frame
ownership under slow USB, stale-frame ordering and descriptor limits for every
profile. They also check endpoint abort ordering, timeout recovery requests,
the early-silicon fallback and DMA byte counting after abort. Short hardware
capture results appear below; long-duration timing, latency and motion quality
still need the remaining physical tests above.

Verified locally on 2026-11-05: all five ESP32 environments and all five Pico
environments built successfully; the USB host tests and existing servo tests
passed. The USB tests ran with AddressSanitizer and UndefinedBehaviorSanitizer.
The current Pico VGA build uses 83,552 bytes of static RAM; HD uses 194,144 bytes. These
figures do not measure runtime stack usage or camera performance. An independent
source review identified the ISO restart issue, which was addressed with a
failing-then-passing regression test and a rebuild of every Pico profile.

Generated default artifacts are `.pio/build/usb_vga30/firmware.bin` (use the
PlatformIO uploader so all ESP32 images receive the correct offsets) and
`pico/.pio/build/zero_vga30/firmware.uf2` (BOOTSEL drag-and-drop). Both boards were
subsequently flashed and verified with the initial VGA30 firmware. The Pico
enumerated as a UVC camera and CDC diagnostics device at a 125 MHz peripheral
clock. The revised GP0/GP1/GP2/GP3 VGA30 mapping was also flashed and verified;
the Pico re-enumerated with working CDC diagnostics. All five Pico profiles
rebuilt and the USB host tests passed after the remap. Subsequent wired testing
and transport fixes are described below.

The first wired test exposed a receive-count bug: RP2040 `CHAN_ABORT` clears
`TRANS_COUNT` ([datasheet, section 2.5](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)).
The receiver now counts completed bytes using the DMA destination address after
abort has flushed pending writes. A regression test reproduces counter clearing
and verifies empty, partial, final-write and overlength cases. With this fix,
VGA frames arrived over SPI at approximately 24.9-25.0 FPS with zero invalid
packets and SPI timeouts during the initial sample. This is a short SPI result,
not a long-soak benchmark. After switching to bulk UVC, macOS AVFoundation/FFmpeg
successfully decoded 192 VGA frames during an eight-second capture. Steady USB
completion counters measured approximately 24.9-25.0 FPS with zero invalid
packets or submission errors during that capture. No footage or audio was saved.
After fixing TinyUSB's endpoint busy/claim reset, three further five-second
captures succeeded across close/reopen cycles (122, 121 and 123 decoded frames).
SPI invalid and timeout counters stayed zero. Closing a stream can cancel an
in-flight frame and briefly increment submission/cancellation counters; those
counters did not keep increasing during the subsequent captures. A 30-minute
moving-scene soak and end-to-end latency measurement remain outstanding.
