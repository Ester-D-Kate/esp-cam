# ESP32-CAM + RP2040 USB camera and programmable button

**Branch: `codex/usb-cam-button` — USB camera plus raw GPIO12 press/release input.** This README
belongs to this branch. Use the README from the branch you actually check out;
the wireless, servo and USB builds have different pin assignments and uploads.

The AI Thinker ESP32-CAM captures JPEG frames, sends them over SPI to a Waveshare
RP2040-Zero, and the RP2040 exposes an MJPEG UVC webcam over its USB port. OBS and
other UVC applications select the resolution and requested FPS. Sensor detection
selects the OV2640 or OV3660 menu. Recurring settings travel back through the Pico;
changing a supported mode does not require another ESP upload.

## Branch map

| Branch | Purpose | Firmware / guide |
|---|---|---|
| [`main`](https://github.com/Ester-D-Kate/esp-cam/tree/main) | Single ESP32-CAM: wireless preview, SD recording, GPIO12 recording button, optional gyro | Root `esp32cam`; that branch's README and `SERIAL_PROTOCOL.md` |
| [`check-1`](https://github.com/Ester-D-Kate/esp-cam/tree/check-1) | Historical backup with single-board sources and archived two-board experiments | Read that branch's README before selecting a source tree |
| [`codex/mqtt-servos`](https://github.com/Ester-D-Kate/esp-cam/tree/codex/mqtt-servos) | Wireless camera with MQTT X/Y servos; SD recording/button disabled | Root `esp32cam`; that branch's README and `SERIAL_PROTOCOL.md` |
| [`codex/usb-cam`](https://github.com/Ester-D-Kate/esp-cam/tree/codex/usb-cam) | Camera baseline: ESP32 SPI → RP2040 USB camera | Root `usb_camera` plus `pico/zero_camera`; [USB guide](USB_CAMERA.md) |
| [`codex/usb-cam-button`](https://github.com/Ester-D-Kate/esp-cam/tree/codex/usb-cam-button) | This branch: USB camera plus programmable press/release HID | Both USB firmwares; button guide below |

The root `esp32cam` environment retained **in this USB branch** is the MQTT-servo
version, not the recording version from `main`. For USB, explicitly select
`usb_camera`. This branch adds a generic HID button alongside video and diagnostics. The
`codex/usb-cam` branch and pre-button tag preserve the camera-only baseline.

## Hardware and wiring

- AI Thinker ESP32-CAM with working PSRAM and OV2640 or OV3660.
- Waveshare RP2040-Zero, USB data cable, short jumper wires, stable ESP32 5 V supply.
- ESP32-CAM serial programmer for the initial ESP upload. Pico uses its own USB.
- Leave the SD socket empty and disconnect servo/gyro wiring for the USB build.

With **both boards powered off**, connect the GPIO labels below. These are not
physical pin positions.

| ESP32-CAM | RP2040-Zero | Function |
|---|---|---|
| IO14 | GP2 | SPI0 clock |
| IO13 | GP0 | ESP → Pico JPEG data |
| IO15 | GP1 | SPI0 chip select |
| IO3 / U0R | GP3 | Pico READY handshake |
| IO2 | GP7 | Pico → ESP configuration |
| GND | GND | Common ground |
| IO12 → existing button → GND | No additional Pico wire | Button, active-low with ESP internal pull-up |
| U0T / IO1 | GP5, optional | ESP logs, only with `zero_camera_uart` |

GP0/GP1/GP2/GP7 use hardware SPI0; GP3 is a GPIO handshake. GP8–GP14 remain unused.
Signals are 3.3 V. Power the Pico by USB and the ESP32 with a stable 5 V supply;
do not power the ESP from Pico 3V3 or join two independent 5 V supplies together.
Keep ESP GPIO0 free during normal operation. GP7 means **GPIO7**, not GPIO6.

## Get the project and build

Install Git, VS Code and the official
[PlatformIO IDE extension](https://docs.platformio.org/en/latest/integration/ide/vscode.html).
PlatformIO includes its CLI in the PlatformIO terminal; no manually created Pico
project is needed. The repository already contains both configured projects.

```sh
mkdir -p ~/Desktop/Codex-projects
cd ~/Desktop/Codex-projects
git clone --branch codex/usb-cam-button https://github.com/Ester-D-Kate/esp-cam.git esp-cam-usb-button
cd esp-cam-usb-button
git branch --show-current
```

Open `usb-camera.code-workspace` in VS Code. In a PlatformIO terminal, from the
repository root:

```sh
pio run -e usb_camera
pio run -d pico -e zero_camera
pio device list
```

PlatformIO downloads the pinned platform, SDK and toolchains on the first build.
ESP uses Arduino/ESP-IDF on `espressif32@7.1.3`; Pico uses the pinned community
RP2040 PlatformIO platform and native Pico SDK, not CircuitPython.

| Pico environment | Board / purpose |
|---|---|
| `zero_camera` | Waveshare RP2040-Zero, normal use |
| `zero_camera_uart` | Same board, plus optional GP5 ESP log input; used for bench validation |
| `pico_camera` | Full-size Raspberry Pi Pico; use the same GPIO numbers |
| `zero_camera_fast` | Legacy-menu experiment; current capable sensors already get their full menus |

## Upload both boards

Close OBS and serial monitors before uploading. Substitute the actual device
port from `pio device list` (`/dev/cu.usbserial-…`, `/dev/ttyUSB…`, or `COM…`).

1. **Upload Pico first.** Hold BOOT, tap RESET, release BOOT on RP2040-Zero, or
   hold BOOT while reconnecting USB. It should appear as `RPI-RP2`.

   ```sh
   pio run -d pico -e zero_camera -t upload
   ```

   Alternatively copy `pico/.pio/build/zero_camera/firmware.uf2` to `RPI-RP2`.
   This replaces CircuitPython/previous firmware; save any files you need first.
   Use `zero_camera_uart` throughout these commands if the GP5 log wire is fitted.

2. **Upload ESP.** Power off and remove the ESP32-CAM from the SPI assembly if
   your programmer needs the whole board. In particular, Pico wires must not
   drive ESP GPIO3 or GPIO2 during programming. Fit the programmer, connect
   GPIO0 to GND and reset/power into download mode.

   ```sh
   pio run -e usb_camera -t upload --upload-port YOUR_ESP_PORT
   ```

   PlatformIO writes the bootloader, partition table and application at the
   correct offsets. Upload speed is 115200 baud.

3. **Restore normal operation.** Power off; remove the programmer, free GPIO0,
   restore the wiring table, keep the SD slot empty, and power both boards.
   Disconnect programmer TX from ESP GPIO3 before normal operation. Reset ESP.
   Allow the specific camera accessory if macOS prompts.

Later Pico uploads can enter BOOTSEL by sending `bootsel` plus Enter to its
Camera Diagnostics serial port at 115200 baud. Physical BOOTSEL remains available.
Supported resolution/exposure/quality adjustments do not need an ESP reflash;
adding new ESP capabilities can. Firmware updates interrupt streaming while the
board restarts.

## Use the camera

In OBS add a **Video Capture Device**, then select **OV3660 Full MJPEG Video** or
**OV2640 Full MJPEG Video**. Choose a custom resolution/FPS where available.
Only run one capture application while testing. Start/stop computer-side
recording in OBS normally; this build is a UVC camera, not a USB storage drive.

The OV3660 menu has 16 sizes through 2048×1536; OV2640 has 14 through 1600×1200.
Selectable FPS values are requests. The full USB/SPI/capture path can deliver
less than the sensor's datasheet limit, especially with detailed scenes or low
light. Faster OV3660 presets crop the field of view.

Measured OV3660 examples: native macOS capture reached about 111 FPS at 160×120,
90 at 320×240 and 58 at 640×480. FFmpeg measured about 41 at 800×600, 29 at
1280×720, 14.8 at 1600×1200 and 9.9 at 2048×1536. Sustained 120 FPS has not been
achieved. All 14 OV2640 sizes were subsequently decoded on hardware, including a 90-second
full-resolution check at about 10 FPS.
See [all measurements, their conditions and limits](USB_CAMERA.md#current-ov3660-results--2026-11-06).

The existing USB **Camera Diagnostics** serial port also accepts:

```text
status
set quality 12
set brightness 1
set hmirror 1
defaults
```

Smaller JPEG-quality values mean better quality/larger frames. Settings are held
in RAM. `defaults` restores the current mode's preset. Check matching
`desired_generation`/`applied_generation`, matching tokens and `config_error:0`;
then check that frame counters actually advance. Detailed controls and recovery
steps are in [USB_CAMERA.md](USB_CAMERA.md).

## Programmable button on the computer

Keep the existing button between **ESP GPIO12 and GND**. Firmware uses
`INPUT_PULLUP`, active-low, with **50 ms debounce on both press and release**.
Do not add an external pull-up: GPIO12 is also a reset strap. Sampling runs every
5 ms independently of camera capture. Holding the button produces one press,
then one release when released; gestures are decided by your computer code.

Both boards need the button firmware once. **Update Pico first, then ESP.** The
new Pico can stream from the previous ESP, but reports the button unavailable
until the ESP is updated. New ESP button headers require the new Pico decoder.
Future PC-side shortcut mappings need no firmware upload. The video sensor
presets, SPI clocks, JPEG limits and requested frame rates are unchanged.

The same physical USB device now has three independent interfaces:

- UVC camera: **OV2640 Full MJPEG Video** or **OV3660 Full MJPEG Video**.
- CDC **Camera Diagnostics**: existing settings/log commands.
- **Camera Button**: vendor-defined HID input (usage page `0xFF00`, usage `1`).

Full-profile VID/PIDs are `CAFE:4064` (OV2640) and `CAFE:4065` (OV3660); discovery
is `CAFE:4060`. The older camera-only firmware used `405x`. macOS may ask to allow
the new accessory, and OBS may require reselecting the camera after the PID change.

This is raw input, not a keyboard shortcut or an automatic recording command.
OBS does not interpret this custom report as its keyboard hotkey. Your reader
can map press/release to recording, holds, double/triple clicks or other actions.
The supplied reader only prints events; it runs beside OBS without opening the
video or serial interfaces.

Install [uv](https://docs.astral.sh/uv/getting-started/installation/), then run:

```sh
uv run tools/usb_button.py --list
uv run tools/usb_button.py
```

The script installs its pinned `hidapi==0.15.0` dependency into uv's environment.
If multiple cameras are connected, select the serial printed by `--list`:

```sh
uv run tools/usb_button.py --serial YOUR_PICO_SERIAL
```

Edit **`on_button(event)` in [tools/usb_button.py](tools/usb_button.py)** to add your
own action. For example, one press/release pair looks like:

```json
{"event":"press","pressed":true,"sequence":1,"timestamp_ms":1000,"session_id":42,"revision":3,"lost":false}
{"event":"release","pressed":false,"sequence":2,"timestamp_ms":1150,"session_id":42,"revision":4,"lost":false}
```

Use ESP `timestamp_ms` differences modulo `2**32` for gesture timing. `sync`,
`unavailable`, `loss` and `disconnected` are state changes, **not clicks**: clear
any pending multi-click/hold logic when they arrive. A sync with `pressed:true`
means the button is already held. The initial held-at-boot button follows the
existing debounce behavior, but opening/reopening a reader does not synthesize
an action for a button already held.

Use one action reader at a time. On open, the example requests a fresh button session, then reads a
snapshot after that request completes; this discards offline history instead of replaying stale
shortcuts. A second reader requesting resync also cancels the first reader's
pending gesture. Reconnects stay with the first selected camera serial number.
On Linux the user needs access to this device's `hidraw` node;
use a device-specific udev rule rather than running the application as root.

### Raw HID report contract

Input **report ID 1** has a 16-byte payload (17 bytes including the report ID).
All multi-byte fields are little-endian. No output endpoint is required.

| Payload offset | Size | Meaning |
|---|---:|---|
| 0 | 1 | Version, `1` |
| 1 | 1 | Flags: pressed `1`, available `2`, resync `4`, loss `8` |
| 2 | 4 | Edge sequence, wrapping at `2**31` |
| 6 | 4 | ESP debounced-edge time in milliseconds, wrapping at `2**32` |
| 10 | 4 | Nonzero button session ID; changes at reboot/resync |
| 14 | 2 | Report revision, wrapping at `2**16`, including lifecycle changes |

`GET_REPORT(1, INPUT)` gives a snapshot with the resync flag. Feature **report ID
2**, one-byte payload `1`, requests a fresh session and current state. Other
feature values have no action. The example filters duplicate/older revisions,
requests resync on gaps, and waits for a fresh available snapshot before actions.
See the [HIDAPI interface](https://trezor.github.io/cython-hidapi/api.html).

ESP and Pico each use fixed 32-event queues. Button metadata and acknowledgements
ride in existing SPI packets; they add no transactions or JPEG bytes. A slow HID
reader cannot hold a camera frame or block UVC submission. Overflow, reboot and
link loss resynchronize state instead of pretending all edges were delivered.
After two seconds without valid SPI headers, the button reports unavailable and
released. Delivery is not persistent while hardware/readers are disconnected.

The SPI button capability is bit `0x4`. It reuses eight diagnostic prefix bytes
and four formerly reserved header bytes, retaining the 128-byte packet header and
CRC. The first eight return-prefix bytes remain available for diagnostics.
Button ACK metadata is excluded from camera configuration tokens and generations.
This architecture avoids waiting on the button in the video path; **simultaneous
physical-button/video performance still requires the on-device checks below**.

## Validation, history and recovery

```sh
sh test/run-usb-cam-tests.sh
sh test/run-servo-tests.sh
uv run --python 3.12 test/usb_cam/test_usb_button.py
pio run -e usb_camera -e esp32cam
pio run -d pico -e zero_camera -e pico_camera -e zero_camera_fast -e zero_camera_uart
```

Host tests require a C/C++ compiler with AddressSanitizer/UndefinedBehaviorSanitizer
and the PlatformIO dependencies installed by the builds. They do not replace
physical button, sensor, power, image-quality or frame-rate checks.

- `usb-cam-first-working` preserves the original fixed-VGA milestone.
- `usb-cam-pre-button-2026-11-06` preserves the camera-only baseline before this feature.
- ESP full-menu source checkpoint: `3460611`; validated Pico source: `6b660ce`.
- [USB_CAMERA.md](USB_CAMERA.md) records fixes and measurements, including failures
  and remaining limits. [Earlier milestone](docs/usb-camera-first-working.md).
- Git history is retained. Check out the tag to inspect/rebuild a checkpoint;
  checking out code does not change the firmware already flashed on either board.

If no frames arrive, first verify common ground, stable ESP power, GPIO0 free,
IO15→GP1, IO3→GP3 and IO2→GP7. Close other capture apps and inspect CDC counters.
The USB branch excludes Wi-Fi/SD recording. Use `main` and its wiring instructions
for the single-board recording build.

Button validation currently includes host debounce/queue/CRC/ACK tests, composite
HID/UVC descriptor tests, actual HID-service failure paths and reader reconnect
checks. These do not establish physical switch behavior or simultaneous video
FPS. After both uploads, test presses, releases, holds and rapid double/triple
presses with OBS active; compare high-rate and high-resolution captures with HID
reading and unread. Existing camera-only measurements in `USB_CAMERA.md` remain
baseline evidence, not measurements of the newly added physical button path.
