# ESP32-CAM firmware guide

This existing document now describes the active single-board firmware. It replaces the old master/slave serial protocol guide, which does not apply to the current `src` build.

## Which source version was which?

These descriptions refer to the versions retained in Git. The archive directories were already deleted in the working tree before this stabilization work; those deletions have been preserved.

| Directory | Purpose |
| --- | --- |
| `src` | Active PlatformIO firmware. Before stabilization, the working copy used the one-board JPEG recording and local gyro implementation. It now supports simultaneous preview and SD recording. |
| `old-src` | Earlier single-board version with MJPEG AVI recording and the gyro on the ESP32-CAM. It matched the previously committed root implementation. |
| `esp-cam-working-backup/src` | Single-board backup: numbered JPEG recording, local MPU6050, CSV gyro messages. This was the basis of the active working copy. |
| `new-src` | Two-board experiment: ESP32-CAM camera/master firmware with a serial link to a helper board. |
| `esp-cam-slave/src` | ESP8266/ESP-12E helper firmware for the two-board experiment, handling gyro, button/status and network/MQTT coordination. It is not a second ESP32-CAM camera firmware. |

Only `src` is built by the root `platformio.ini`.

## Current operation

- AI Thinker ESP32-CAM pin layout with an **OV3660 or OV2640** and working PSRAM; native portrait JPEG at **600 × 800**, quality 12, target **20 FPS**.
- The firmware detects the sensor PID and selects its window API: OV3660 scales a centred 1168 × 1536 active crop to 608 × 800; OV2640 scales a centred 900 × 1200 crop to 600 × 800. The OV3660 needs complete 16-pixel JPEG blocks: its 608-pixel capture includes the last block, then the firmware changes the JPEG's visible width to 600 to hide eight right-edge pixels. Compressed image data is preserved. Startup verifies dimensions and, for this crop, the expected YUV422 sampling; unsupported sensors or a failed portrait configuration leave the camera unavailable with a serial diagnostic.
- The capture worker and port-81 preview server run on core 1; the SD writer runs on core 0, where higher-priority Wi-Fi/SDK tasks can preempt it. Capture immediately returns the camera driver's buffers after copying into fixed PSRAM slots. The workers share frames through bounded queues and short mutex sections; storage and network I/O run outside the mutex.
- Six 160 KiB slots consume 960 KiB of PSRAM. The driver has two additional JPEG buffers of about 154 KiB each, sized with the XGA allocation preset; the actual output remains 600 × 800. Queues and task stacks are also allocated at startup.
- Recording writes numbered JPEGs and `frames.csv` under `/recordings/<local-date-time>/`. Before NTP sync, a persistent numeric session ID is used.
- JPEG writes copy up to 8 KiB at a time into one fixed internal DMA buffer and use direct POSIX file writes through the SD mount. This avoids stdio buffers allocated in PSRAM and the SDK's slow per-sector PSRAM fallback. The writer yields between chunks, checks open/write/close results and attempts to remove incomplete JPEGs on write or close failure. The buffer is allocated once at camera startup and freed if startup fails; its 8 KiB internal RAM usage is additional to the PSRAM slots and static RAM reported by the build.
- The timestamp index also uses direct file writes, with a checked sync once per second and a checked close before recording becomes idle. A failed index write, sync or close reports a metadata error and stops the session. The first media error is retained so later cleanup failures do not hide the original cause. A faulty card can still leave incomplete files or CSV rows; the recording is reported as failed.
- A separate HTTP server on **port 81** sends the newest available frame. The setup/status server remains on **port 80**.
- The recording queue holds at most two frames. Slow SD writes drop new recording frames and count them; they do not block the preview task. A slow preview client holds one frame and skips older frames. Successful writes can finish the JPEG without a total frame deadline; each socket write has a five-second timeout. Send failure or three seconds without a new camera frame closes the connection and logs the reason.
- One active preview viewer is supported. Close other viewer tabs if a new preview cannot connect. Additional sockets are bounded and can wait behind the active MJPEG request.
- Wi-Fi disconnection closes preview but **does not stop SD recording**. The setup AP returns, and saved Wi-Fi credentials are retried about every 30 seconds. Preview reconnects once Wi-Fi returns.
- The physical button toggles recording even while preview is open. Stop drains the accepted frames and closes storage before another session can start. Wait until recording shows **Idle**, then power off before removing the SD card.
- Missing/invalid/oversized camera frames and short SD writes are detected. Repeated capture errors stop the recording with an error. After an SD fault, a new button press attempts to remount the card.
- Gyro sampling runs independently of recording state. MQTT connects in its own SDK task, with a bounded outgoing queue. Disconnected MQTT samples are disposable. The CSV payload remains `yaw,pitch,roll`.

Twenty FPS is a target, not a measured guarantee. SD file creation/write speed, scene detail, lighting/exposure, Wi-Fi signal and power affect throughput. In particular, saving a separate JPEG file for each frame has filesystem overhead. Fixed queues keep memory and latency bounded by dropping work instead of building an ever-growing backlog.

## Portrait output and receiver rotation

The direct **`:81/stream`** endpoint and SD JPEGs contain **600 × 800 portrait** frames in sensor orientation. The ESP32 does not decode, rotate or re-encode them. A receiving device can rotate them by 90° to obtain **800 × 600 landscape**.

On the user's OV3660, configuring 600 pixels directly produced malformed JPEGs: three captured files advertised 600 × 800 but contained only 37 × 100 complete 16 × 8 blocks instead of the required 38 × 100. This caused shifted image bands and a decoder error. Capturing 608 × 800 and reducing only the visible width to 600 preserves the full 38-block rows; a decoder discards the final eight pixels as normal JPEG edge padding. Host checks confirm the production header change preserves every visible decoded pixel. New on-device frames must still confirm the sensor now emits all 3800 blocks and decodes correctly.

The main viewer defaults to **0° — portrait**. Choose **90° clockwise** or **90° counterclockwise** for landscape to suit the physical mounting. This browser preference is saved separately from the old landscape-camera setting. The conversion script defaults to clockwise rotation and therefore produces an 800 × 600 video from these new recordings; use `-Rotation none` to keep portrait. See `tools/command.md`.

OV3660 and OV2640 interpret the same `set_res_raw` interface differently: the [OV3660 driver](https://github.com/espressif/esp32-camera/blob/v2.0.4/sensors/ov3660.c#L876) uses start/end coordinates and line timings, whereas the [OV2640 driver](https://github.com/espressif/esp32-camera/blob/v2.0.4/sensors/ov2640.c#L455) uses a sensor mode and DSP window. They have separate configurations. The OV2640 path uses UXGA readout, which its [manufacturer rates at up to 15 FPS](https://dl.sipeed.com/MAIX/HDK/Chip_DS/OV2640-DATASHEET.pdf). That limit is specific to the OV2640 path. The user's OV3660 baseline measured about 15 FPS capture/preview before recording; see the recording diagnosis below. The 20 FPS scheduler remains an upper target.

## Pins and setup

| Function | Pin/configuration |
| --- | --- |
| Camera | AI Thinker camera pins from `src/camera_pins.h` |
| SD card | SD_MMC, 1-bit mode: GPIO14 clock, GPIO15 command, GPIO2 data |
| Record button | GPIO12 to GND, active LOW with runtime internal pull-up |
| Indicator | GPIO4; steady ON during recording and the 15-second success window, toggles every 500 ms while connecting and every 100 ms on errors, OFF otherwise |
| MPU6050 | SDA GPIO3, SCL GPIO13, address 0x68 |
| Serial logs | UART0 TX on GPIO1, 115200 baud |

GPIO12 is also a boot strap pin. A pull-up from external wiring or the SD card's DAT2 connection can select the wrong flash voltage during reset on affected boards; an internal pull-up configured after boot does not change the reset strap sampling. Check cold boot with the actual card and button wiring. The firmware retains the user's pin assignment. Disconnect programmer TX from GPIO3 during normal gyro use. Keep the gyro still during startup calibration.

Without saved Wi-Fi credentials, join **ESP32-CAM-Setup** (password **12345678**) and open **http://192.168.4.1/setup**. Once connected, use **http://esp32.local/** or the station IP shown in serial/status. The viewer handles port 81, rotation and automatic reconnection. It retries after image errors or 15 seconds without preview progress, tolerating brief Wi-Fi stalls. A direct client can use `http://<station-IP>:81/stream`; `/stream` on port 80 redirects there. The raw MJPEG URL has no browser reconnection script: open the main viewer for automatic recovery and close the raw-stream tab.

Gyro authentication is optional and does not prevent camera/Wi-Fi operation if it fails. HTTPS uses the pinned SDK's CA trust bundle and hostname verification; NTP time must be valid before authenticating. Self-signed/private CA servers are not trusted by default. Plain HTTP is retained for existing local validation servers and sends credentials without TLS. The existing gyro MQTT endpoint remains the public, unencrypted `broker.hivemq.com:1883`, topic `eyetracker/<uuid>/gyro`. Each MQTT client instance uses a random identifier for its lifetime, rather than exposing the device MAC. There is no login on the local camera/setup endpoints, so this firmware is intended for a trusted LAN/AP, not direct Internet exposure.

The MJPEG endpoint does not grant cross-origin JavaScript access. The built-in viewer embeds the port-81 stream as an ordinary image, which does not require wildcard CORS. Native raw-stream clients continue to use the same endpoint; a separate web application needing to read pixels would require an explicitly permitted origin.

## Useful diagnostics

`/status` reports recording/stopping state, saved/dropped frames, preview/capture counts, capture errors, oversized frames, slowest SD write in milliseconds, free heap/PSRAM, media errors and separate gyro/MQTT readiness. Capture/preview counters are lifetime totals; recording counts and slowest SD write reset for each new session.

On 4 October, the previous firmware measured about 14–15 FPS preview alone, dropping to roughly 2–5 FPS during recording. Over about 81 seconds of recording, capture averaged 15.6 FPS, preview 3.3 FPS and saved frames 7.1 FPS; the slowest successful JPEG write took 2104 ms. Capture errors stayed at zero and PSRAM usage stayed stable during that sample. The [installed IDF 4.4.7 SD driver](https://github.com/espressif/esp-idf/blob/v4.4.7/components/sdmmc/sdmmc_cmd.c) writes non-DMA sources through a temporary single-sector buffer. The new DMA staging and core placement address that overhead; they have passed host checks and compilation, but require a new upload and measurement to establish the actual improvement. An SD card's internal stalls and per-file metadata cost still affect recording throughput.

Authentication bodies have a 4096-byte cap and a five-second read deadline, including chunked and connection-close responses. Normal URL-encoded setup POST bodies are read with a 4096-byte cap, a four-second deadline and at most eight fields before argument parsing. The legacy Arduino control server still parses request headers, query strings and multipart inputs itself; it is not hardened against arbitrary hostile HTTP traffic.

The web page shows recording progress and SD drop counts while preview is active. Wi-Fi scans are refused during active recording/preview, because scans temporarily interrupt radio traffic.

## Build and validation

The PlatformIO platform is pinned to `espressif32@7.1.3`; camera, HTTP, MQTT and JSON APIs come from its matching Arduino/ESP-IDF SDK. Build with:

```sh
platformio run -e esp32cam
```

Uploads use **115200 baud**. Close the Serial Monitor before uploading. If connection/flash detection fails, power off, remove the SD card, and enter download mode with GPIO0 low during reset. After uploading, power off, remove any GPIO0-to-GND jumper, reinstall the SD card and power on for normal operation.

The changes have been compiled with the installed ESP32 toolchain. Host regression artifacts are under `~/Desktop/Codex-projects/esp-cam-validation/`, including the original working source snapshot. The host harness compiles the actual media implementation with hardware adapters, runs concurrent threads and checks failure cleanup with AddressSanitizer/UndefinedBehaviorSanitizer. These checks do not emulate ESP32 scheduling, PSRAM bandwidth, camera DMA, SD electrical behavior or radio performance.

Before relying on recordings, validate on the board:

1. Cold boot with the intended card/button/gyro wiring and a suitable power supply. Confirm camera, PSRAM and SD initialize.
2. Open one viewer and start recording with the button. Confirm `/status` keeps responding, JPEGs grow and the raw stream is 600 × 800 portrait; a 90° receiver/viewer rotation must display 800 × 600 landscape.
3. Record for at least 30 minutes; observe heap/PSRAM, drop counts and SD write times. Export the result using `frames.csv` and check timing/orientation.
4. Disconnect/reconnect Wi-Fi while recording; confirm the same directory keeps receiving frames and preview recovers.
5. Stop/start repeatedly. Check that each finished session has complete JPEGs and a readable frame index.
6. Exercise missing/full SD, unavailable MQTT broker and disconnected gyro. A media failure must be reported without permanently disabling the camera/network or leaking memory across retries.

The FFmpeg concat format was exercised with 600 × 800 sample images: clockwise rotation produces 800 × 600 output and frame timestamp gaps are preserved. The PowerShell wrapper itself was not executed because PowerShell is not installed on this host.

An earlier build was uploaded successfully on **4 October 2026**, at 115200 baud with the SD card removed and the board in download mode. All flashed image hashes verified; flash identification reported 4 MB at 3.3 V. The Serial Monitor had to be closed to release the port. The restored LED logic, stream timeout correction and native portrait changes require a new upload; the user will upload them. On-device dimensions, FPS and soak testing are still required. Sudden power removal can still lose pending frames or filesystem metadata; software cannot make an SD card power-loss-proof.
