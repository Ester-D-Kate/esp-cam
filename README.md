# ESP32-CAM wireless preview and SD recording

**Branch: `main`.** This README describes the root `src` build on this branch.
It runs on one AI Thinker ESP32-CAM with PSRAM and OV2640 or OV3660. It provides
wireless MJPEG preview, recording to the built-in SD socket, a GPIO12 recording
button and optional MPU6050 telemetry. It is not the RP2040 USB-camera build.

## Choose the correct branch

| Branch | Purpose |
|---|---|
| [`main`](https://github.com/Ester-D-Kate/esp-cam/tree/main) | This single-board Wi-Fi/SD recording firmware |
| [`check-1`](https://github.com/Ester-D-Kate/esp-cam/tree/check-1) | Historical sources and archived ESP8266 helper experiment |
| [`codex/mqtt-servos`](https://github.com/Ester-D-Kate/esp-cam/tree/codex/mqtt-servos) | Wireless camera plus MQTT servos; SD/button disabled |
| [`codex/usb-cam`](https://github.com/Ester-D-Kate/esp-cam/tree/codex/usb-cam) | ESP32 SPI → RP2040 UVC USB webcam, with its own wiring and uploads |

Each branch has its own README. Do not mix pin assignments or flash targets.
The backup directory is retained for reference; root PlatformIO builds only `src`.

## Install, build and upload

Install Git, VS Code and the official PlatformIO IDE extension. Its PlatformIO
terminal includes `pio`. Clone this branch into your projects directory:

```sh
mkdir -p ~/Desktop/Codex-projects
cd ~/Desktop/Codex-projects
git clone --branch main https://github.com/Ester-D-Kate/esp-cam.git esp-cam-wireless
cd esp-cam-wireless
pio run -e esp32cam
pio device list
```

Open this repository folder in VS Code. PlatformIO installs the pinned
`espressif32@7.1.3` platform and its matching Arduino/ESP-IDF dependencies.
Close serial monitors, power off, remove the SD card, place ESP32-CAM on its
programmer, connect GPIO0 to GND and reset/power into download mode. Then:

```sh
pio run -e esp32cam -t upload --upload-port YOUR_ESP_PORT
```

Replace `YOUR_ESP_PORT` with the programmer's port, such as `/dev/cu.usbserial-…`,
`/dev/ttyUSB0` or `COM5`. Upload uses 115200 baud. Afterwards power off, remove the
GPIO0-to-GND jumper, remove the programmer, restore normal wiring/card and power
on. Serial logs use 115200 baud; this branch's old monitor default is 9600, so use:

```sh
pio device monitor --port YOUR_ESP_PORT --baud 115200
```

## Wiring and use

| Function | Connection |
|---|---|
| Camera | AI Thinker pin map in `src/camera_pins.h` |
| SD socket | 1-bit SDMMC: GPIO14 clock, GPIO15 command, GPIO2 data |
| Recording button | GPIO12 to GND; active-low, runtime internal pull-up, 50 ms debounce |
| Indicator | GPIO4 |
| Optional MPU6050 | SDA GPIO3, SCL GPIO13, address 0x68 |
| Serial logs | GPIO1/U0T, 115200 baud; disconnect programmer TX from GPIO3 during gyro use |

Use a stable 5 V supply and common ground. GPIO12 is a boot strap: keep the
existing button-to-GND wiring, without adding an external pull-up. Check cold
boot with the intended card. Power off before changing wiring or removing SD.

With no saved Wi-Fi credentials, join **ESP32-CAM-Setup**, password **12345678**,
and open **http://192.168.4.1/setup**. Configure Wi-Fi, then open
**http://esp32.local/** or the IP in serial logs. One preview viewer is supported;
the raw stream is `http://DEVICE_IP:81/stream`.

Press the GPIO12 button to start recording, then press again to stop. Wait for
**Idle** before powering off/removing the SD card. Recordings contain numbered
JPEGs and `frames.csv` under `/recordings/`. See [conversion instructions](tools/command.md).
Preview and recording share bounded buffers; a slow card can drop recording
frames. Target FPS is not a guarantee of delivered or recorded FPS.

Sensor-specific default output is OV2640 800×600 landscape or OV3660 600×800
portrait (608×800 encoded with visible-width trimming). This branch has no
OBS-selectable UVC menu. See [the full firmware guide](SERIAL_PROTOCOL.md) for
sensor geometry, recording limitations, status fields and hardware checks.

The main branch's firmware and prior Git history are preserved; this README
update changes documentation only. USB firmware development lives separately.
