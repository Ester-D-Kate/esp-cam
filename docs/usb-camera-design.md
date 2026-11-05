# USB POV camera design

## Goal and scope

Build a USB-only POV camera from the existing AI Thinker ESP32-CAM and Waveshare
RP2040-Zero. Prioritize image quality, motion consistency and low latency over
optional SD recording, Wi-Fi, servos and gyro control. These remain in the legacy
PlatformIO environment; they are excluded from the USB firmware. Branch:
`codex/usb-cam`, based on `fe4321f`. Work stays in this repository as explicitly
requested, overriding the general new-project directory preference.

Both boards have been flashed and wired. SPI reception measured approximately
24.9-25.0 FPS at VGA with no invalid packets in the initial sample. macOS decoded
192 frames in an eight-second USB capture using bulk UVC. Long-duration FPS,
latency and motion quality still need measurement. No claim of zero loss,
HD/60 FPS, stabilization or equivalence to consumer camera glasses.

## Research and decisions

* [RP2040 datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf),
  sections 4.1 and 4.4: USB full-speed is 12 Mbit/s; SPI slave clock must be at
  most clk_peri/12. Continuous bytes under one CS require CPHA=1. Use SPI mode 1,
  8 MHz, RX DMA, plus a READY wire. Do not overclock either chip.
* [TinyUSB UVC example](https://github.com/hathach/tinyusb/tree/master/examples/device/video_capture)
  and [video API](https://docs.tinyusb.org/en/latest/reference/class/video.html):
  MJPEG UVC; retain frame memory until completion. Include CDC diagnostics in
  the same composite USB device. The current firmware uses a 64-byte full-speed
  bulk endpoint and a 2048-byte staging buffer.
* [GB Interceptor](https://github.com/Staacks/gbinterceptor): RP2040 UVC prior art.
  Its bulk UVC transport informed the hardware-tested USB path. Its synthetic
  Game Boy video does not establish achievable natural-scene FPS.
* [ArduCAM PICO_SPI_CAM](https://github.com/ArduCAM/PICO_SPI_CAM): SPI camera
  acquisition prior art; requires its host software, not a drop-in ESP32 UVC bridge.
* [Espressif camera driver](https://github.com/espressif/esp32-camera): keep JPEG
  sensor compression and continuous double buffering in PSRAM. Avoid JPEG decode,
  software rotation, resize or recompression on these microcontrollers.
* [ESP32 SPI master](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/spi_master.html):
  transfer from internal DMA-capable memory, staging PSRAM in bounded chunks.
* [ESP32 video recorder](https://github.com/jameszah/ESP32-CAM-Video-Recorder-junior):
  sequential MJPEG/AVI recording is preferable to opening a JPEG per frame, but
  recording is excluded from this first USB-only deliverable.
* [PlatformIO Pico](https://docs.platformio.org/en/latest/boards/raspberrypi/pico.html)
  officially exposes Arduino. The pinned
  [maxgerhardt platform](https://github.com/maxgerhardt/platform-raspberrypi)
  also exposes native `framework = picosdk`, allowing direct TinyUSB/DMA control.
  Use its actual Waveshare RP2040-Zero board definition, plus a Pico environment.
* [Waveshare RP2040-Zero](https://www.waveshare.com/wiki/RP2040-Zero): use exposed
  GP0/GP1/GP2 for SPI0 RX/CS/SCK, GP3 for READY. Reserve GP8 through GP14 per
  the user's wiring constraint. Avoid RGB LED GP16.

Alternatives considered: SD + Wi-Fi + USB adds contention and pin pressure;
Pico as SPI master requires a reverse configuration/handshake protocol; generic
USB serial video requires a host application. The selected USB-only transmitter
and UVC receiver best match the user's latest priority and existing hardware.

## Profiles and bandwidth

Both builds select the same `USB_CAM_PROFILE` from one shared header:

| ID | Dimensions | Target FPS | JPEG ceiling | Initial JPEG quality |
|---|---|---|---|---|
| 1 (default) | 640 x 480 | 30 | 24 KiB | 16 |
| 2 | 800 x 600 | 15 | 48 KiB | 12 |
| 3 | 1280 x 720 | 10 | 60 KiB | 16 |
| 4 | 320 x 240 | 30 | 24 KiB | 12 |

These are test targets, not measured capabilities. USB advertises exactly the
selected profile. Higher JPEG quality means a smaller numeric quality setting.
On oversize JPEGs, increase compression within a bounded range and count the
lost frame. Do not silently change dimensions, invent frames or advertise
uncompressed video. No automatic quality recovery oscillation within a run.

## Hardware contract

ESP32 GPIO14 SCK -> Zero GP2; GPIO13 MOSI -> GP0; GPIO15 CS -> GP1;
Zero GP3 READY -> ESP32 GPIO3; common ground. All signals 3.3 V.
The receiver uses SPI0, including its RX DMA request. GP8 through GP14 remain
unused by the Pico firmware. This revises the original SPI1 pinout.
Remove the SD card and detach servos/gyro for this firmware. GPIO12 remains free,
but recording/button functionality is not active in the USB build.
UART TX stays GPIO1. Disconnect programmer TX from GPIO3 during operation and
disconnect READY while flashing ESP32. Supply stable power; never join competing
5 V outputs. Select suitable power wiring after checking the actual boards.

## Transport and ownership

ESP32 captures JPEGs, validates dimensions, trims trailing padding after EOI,
and transmits a 48-byte little-endian header followed by the JPEG. Header fields:
magic, version/header size, profile, sequence, timestamp_us, payload size,
payload CRC32, capture-error count, oversize count, link-timeout count, JPEG
quality, reserved bytes and header CRC32. All fields and limits are validated.

Pico core 1 arms a receive buffer before setting READY high. ESP32 waits for
READY high, asserts CS, and waits for READY low (receiver acknowledgment) before
clocking. CS remains low across header/chunk transfers; ESP32 waits 10 us after
the last clock, releases CS, and enforces at least 100 us idle. Pico polls CS on
its dedicated core, completes DMA when CS rises, rejects truncated/oversize/corrupt frames and rearms for
the next transaction. Timeouts recover if either board reboots or a wire opens.

Three bounded Pico slots separate receiving, validation/ready and USB in-flight
ownership. The oldest ready frame may be replaced to keep latency bounded;
never overwrite an in-flight USB frame. Core 0 owns all TinyUSB calls. SPI IRQs
are unnecessary for SPI; core 1 polls while DMA receives. Critical sections
protect only slot state, never CRC or I/O. Unplug/stop resets USB ownership only
after TinyUSB stops using the buffer. A small adapter compiles the pinned video
driver unmodified and exposes its actual retained pointer; callback-only or
streaming-flag-only tracking would miss some cancellations. An extra DMA byte
detects overlength transfers. A timed-out low CS keeps READY low until released.

The pinned TinyUSB RP2040 driver leaves pending ISO packets intact on endpoint
reactivation. A project-local adapter applies the upstream abort sequence before
activation, with a bounded wait and full-controller reconnect fallback. This
also avoids unsafe endpoint abort on B0/B1 silicon (RP2040-E2). The actual helper
is tested using a register model; hardware reopen/unplug testing remains required.

The initial isochronous build caused a macOS USB pipe error, so the current
firmware uses bulk UVC. On endpoint reopen, the adapter reuses its DPRAM
allocation, aborts pending hardware transfers, and resets both TinyUSB busy and
claimed state. The host test models those flags independently. SPI receive
length comes from DMA WRITE_ADDR after abort completes: RP2040 CHAN_ABORT clears
TRANS_COUNT, so reading that counter afterward incorrectly reports a full buffer.

## Verification

Host tests exercise the shared wire parser, CRC, JPEG dimensions, profile limits,
corruption/truncation, time/sequence wrap and buffer ownership under slow USB.
Compile both targets and legacy ESP32. Hardware acceptance: enumerate on the
host, confirm negotiated dimensions, measure completed USB FPS and drops during
bright/dim/moving scenes, reconnect either board, stall the host, and run a long
soak. Read diagnostic counters and host timestamps; descriptor FPS alone is not
proof. Document artifacts, wiring, flashing, source pins, profile matching and
all unverified physical behavior before delivery.
