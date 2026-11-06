# ESP32-CAM wireless camera with MQTT servos

**Branch: `codex/mqtt-servos`.** This firmware runs on one AI Thinker ESP32-CAM
with OV2640 or OV3660 and PSRAM. It retains wireless camera preview and optional
gyro telemetry, adding two MQTT-controlled servos. SD recording and the GPIO12
recording button are disabled because the servo signals occupy SD pins.

## Branch map

| Branch | Purpose |
|---|---|
| [`main`](https://github.com/Ester-D-Kate/esp-cam/tree/main) | Single-board Wi-Fi preview and SD recording/button |
| [`check-1`](https://github.com/Ester-D-Kate/esp-cam/tree/check-1) | Historical backup and archived helper-board experiments |
| [`codex/mqtt-servos`](https://github.com/Ester-D-Kate/esp-cam/tree/codex/mqtt-servos) | This wireless camera + MQTT servo build |
| [`codex/usb-cam`](https://github.com/Ester-D-Kate/esp-cam/tree/codex/usb-cam) | ESP32 + RP2040 USB webcam; different firmware and wiring |
| [`codex/usb-cam-button`](https://github.com/Ester-D-Kate/esp-cam/tree/codex/usb-cam-button) | USB camera plus raw GPIO12 press/release HID; use its own README |

Use each branch's own README. The root `src` is active; the backup directory is
reference material. No RP2040 is used by this branch.

## Install, build and upload

Install Git, VS Code and the official PlatformIO IDE extension. Its terminal
includes `pio`. Open the cloned repository folder in VS Code:

```sh
mkdir -p ~/Desktop/Codex-projects
cd ~/Desktop/Codex-projects
git clone --branch codex/mqtt-servos https://github.com/Ester-D-Kate/esp-cam.git esp-cam-servos
cd esp-cam-servos
pio run -e esp32cam
sh test/run-servo-tests.sh
pio device list
```

PlatformIO installs the pinned `espressif32@7.1.3` SDK/toolchain. Host tests need
Clang or another C/C++ compiler supporting address/undefined-behavior sanitizers.
Close serial monitors; power off, remove the SD card, fit the ESP programmer,
connect GPIO0 to GND and reset/power into download mode. Substitute the actual
programmer port (`/dev/cu.usbserial-…`, `/dev/ttyUSB0`, or `COM5`):

```sh
pio run -e esp32cam -t upload --upload-port YOUR_ESP_PORT
```

Upload is 115200 baud. Power off afterwards, remove the GPIO0 jumper and programmer,
restore normal wiring and power on. Keep the SD socket empty. Serial logs:

```sh
pio device monitor --port YOUR_ESP_PORT --baud 115200
```

## Wire and operate

| Function | Connection |
|---|---|
| X servo signal | ESP GPIO14 |
| Y servo signal | ESP GPIO15 |
| Servo power | Separate supply appropriate for the servos; join GND with ESP |
| ESP power | Stable 5 V supply; do not power servos from ESP 3V3 |
| Optional MPU6050 | SDA GPIO3, SCL GPIO13, address 0x68 |
| Indicator | GPIO4 |
| Camera | AI Thinker map in `src/camera_pins.h` |
| SD / GPIO12 button | Disabled; leave SD socket empty |

Both servos request 90° after camera initialization on boot. PWM is 50 Hz with
500–2500 µs default endpoints; check those limits against your servo/mechanism
before exercising full travel. Power off before wiring; test GPIO15 boot behavior
with the actual servo connection. Disconnect programmer TX from GPIO3 for gyro use.

Join **ESP32-CAM-Setup** (password **12345678**), open
**http://192.168.4.1/setup**, and configure Wi-Fi. Open **http://esp32.local/** or
the logged station IP. One preview viewer is supported; raw MJPEG uses
`http://DEVICE_IP:81/stream`. OV2640 defaults to 800×600 landscape; OV3660 to
600×800 portrait. Requested FPS depends on exposure, scene detail and Wi-Fi.

The public, unauthenticated MQTT broker is `broker.hivemq.com:1883`; the fixed
command topic is `esp32cam/kate/servo/delta`. Send nonretained QoS0 JSON degree
deltas such as `{"x":10,"y":-5}`. Positions clamp to 0–180°; each duplicate message
moves again. Anyone able to publish on this public topic can control the servos.
See [SERIAL_PROTOCOL.md](SERIAL_PROTOCOL.md) for command validation, status,
power/wiring details and physical verification requirements.

This documentation checkpoint changes no servo/camera firmware. The USB camera
and programmable USB button feature are separate branches, not additions here.
