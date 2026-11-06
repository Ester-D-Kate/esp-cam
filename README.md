# ESP32-CAM historical backup

**Branch: `check-1`.** This is an archived development checkpoint, not the tested
RP2040 USB camera. Preserve its source history when comparing experiments.

## Branch map

| Branch | Purpose |
|---|---|
| [`main`](https://github.com/Ester-D-Kate/esp-cam/tree/main) | Later single-board wireless preview and SD recording |
| [`check-1`](https://github.com/Ester-D-Kate/esp-cam/tree/check-1) | This historical backup |
| [`codex/mqtt-servos`](https://github.com/Ester-D-Kate/esp-cam/tree/codex/mqtt-servos) | Wireless camera with MQTT X/Y servos |
| [`codex/usb-cam`](https://github.com/Ester-D-Kate/esp-cam/tree/codex/usb-cam) | ESP32 SPI → RP2040 USB webcam; use its README for USB setup |

## Select the source before building

The root `platformio.ini` builds **root `src` only**: a single ESP32-CAM with
local GPIO12 recording button, SD storage and optional local gyro. `old-src`,
`new-src` and `esp-cam-working-backup/src` are archived alternatives and are not
selected by that build. Do not rename/copy these directories unless intentionally
reconstructing that experiment.

`esp-cam-slave/` is a separate ESP8266/ESP-12E PlatformIO project. Its accompanying
[SERIAL_PROTOCOL.md](SERIAL_PROTOCOL.md) describes the archived two-board serial
experiment associated with `new-src`; **that wiring does not describe root `src`**.
The ESP8266 helper is not a Raspberry Pi Pico USB camera bridge.

## Build and upload the root single-board backup

Install Git, VS Code and PlatformIO IDE. Open its terminal, then:

```sh
mkdir -p ~/Desktop/Codex-projects
cd ~/Desktop/Codex-projects
git clone --branch check-1 https://github.com/Ester-D-Kate/esp-cam.git esp-cam-check-1
cd esp-cam-check-1
pio run -e esp32cam
pio device list
```

This historical configuration uses unpinned `espressif32` and the library ranges
in `platformio.ini`. It has not been rebuilt or hardware-validated as part of the
USB work; for current single-board use, prefer `main` and its instructions.

Close serial monitors. Power off, remove the SD card, fit the ESP programmer,
connect GPIO0 to GND and reset/power into download mode. Substitute its port:

```sh
pio run -e esp32cam -t upload --upload-port YOUR_ESP_PORT
```

This branch configures upload speed 921600; if the adapter/wiring cannot sustain
it, set `upload_speed = 115200` in `platformio.ini`. After upload, power off,
free GPIO0, remove the programmer, restore wiring/card and power on. Monitor:

```sh
pio device monitor --port YOUR_ESP_PORT --baud 115200
```

Root `src` wiring: AI Thinker camera pins; 1-bit SDMMC GPIO14/15/2; button GPIO12
to GND with runtime internal pull-up; indicator GPIO4; optional MPU6050 SDA GPIO3,
SCL GPIO13. Use stable 5 V power, common ground and no external GPIO12 pull-up
because it is a boot strap. Disconnect programmer TX from GPIO3 during gyro use.

Join **ESP32-CAM-Setup** (password **12345678**) and open
**http://192.168.4.1/setup** to configure Wi-Fi; then use **http://esp32.local/**
or its station IP. The button toggles SD recording. Stop recording before power
off/card removal. This checkpoint makes no current frame-rate or reliability claim.

For the archived ESP8266 helper only, open `esp-cam-slave/` as a separate project:

```sh
pio run -d esp-cam-slave -e esp12e
pio run -d esp-cam-slave -e esp12e -t upload --upload-port YOUR_ESP8266_PORT
```

Use the ESP8266 programmer/boot procedure appropriate to that board and the
archived protocol's wiring. Do not connect it using the RP2040 SPI wiring.
This README commit preserves all firmware and historical directories unchanged.
