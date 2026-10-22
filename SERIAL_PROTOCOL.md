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

- AI Thinker ESP32-CAM pin layout with an **OV3660 or OV2640** and working PSRAM; **OV3660: 600 × 800 portrait**, **OV2640: 800 × 600 landscape**, quality 12, target **20 FPS**. Other valid, aligned native sizes in the selected sensor's orientation are accepted and reported as their actual dimensions.
- The firmware detects the sensor PID and selects its configuration: OV3660 scales a centred 1168 × 1536 active crop to 608 × 800; OV2640 uses the SDK's standard SVGA landscape setting and full landscape field of view. For the OV3660's known 608 × 800 YUV422 profile, the firmware changes only the JPEG's visible width to 600 to hide eight right-edge pixels; compressed image data is preserved. Other aligned sizes or sampling layouts keep their native headers. Within six capture attempts, startup discards the two initial driver buffers, then validates the baseline header, sampling, block alignment, orientation and sensor bounds and requires two consecutive matching frames before selecting the encoded/output geometry for the boot. Subsequent frames must retain that geometry so a recording does not mix dimensions. Unsupported sensors, failed capture or invalid frames still produce diagnostics.
- The capture worker and port-81 preview server run on core 1; the SD writer runs on core 0, where higher-priority Wi-Fi/SDK tasks can preempt it. Capture immediately returns the camera driver's buffers after copying into fixed PSRAM slots. The workers share frames through bounded queues and short mutex sections; storage and network I/O run outside the mutex.
- Six 160 KiB slots consume 960 KiB of PSRAM. The driver has two additional JPEG buffers of about 154 KiB each, sized with the XGA allocation preset; the actual output is selected from matching valid JPEGs. Queues and task stacks are also allocated at startup. Native-size adaptation does not increase these byte limits.
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

## Sensor orientation and receiver rotation

The direct **`:81/stream`** endpoint and SD JPEGs use the same sensor-specific orientation:

| Sensor | Preferred output | Capture configuration |
| --- | --- | --- |
| OV3660 | 600 × 800 portrait | Custom portrait window, aligned 608 × 800 encoding |
| OV2640 | 800 × 600 landscape | Standard SVGA landscape readout |

If a supported sensor produces a different valid aligned size in its required orientation, that size is preserved. `/status` and the main viewer report the selected output width/height. The ESP32 does not decode, rotate or re-encode frames. A receiving device's 90° rotation swaps width and height: OV3660's 600 × 800 becomes 800 × 600; OV2640's 800 × 600 becomes 600 × 800.

On the user's OV3660, configuring 600 pixels directly produced malformed JPEGs: three captured files advertised 600 × 800 but contained only 37 × 100 complete 16 × 8 blocks instead of the required 38 × 100. This caused shifted image bands and a decoder error. Capturing 608 × 800 and reducing only the visible width to 600 preserves the full 38-block rows; a decoder discards the final eight pixels as normal JPEG edge padding. Host checks confirm the production header change preserves every visible decoded pixel. New on-device frames must still confirm the sensor now emits all 3800 blocks and decodes correctly.

On 14 October, OV2640 logs from the earlier portrait configuration established that a 600-pixel DSP request produced **592 × 800** JPEGs with sampling 21/11/11, while the registers correctly read back the requested 600-pixel setting. The current OV2640 configuration uses standard **800 × 600 landscape**, as requested, and no longer applies that custom portrait crop. JPEG validation checks the actual encoded dimensions rather than requiring the preferred size exactly.

The main viewer defaults to **0° — native orientation** for each sensor. Choose **90° clockwise** or **90° counterclockwise** to suit the physical mounting. Browser rotation preferences are saved separately for portrait and landscape sources; the old shared portrait preference is ignored. The conversion script still defaults to clockwise rotation. Pass `-Rotation none` to preserve either sensor's native orientation, including normal OV2640 landscape. See `tools/command.md`.

The [OV3660 driver](https://github.com/espressif/esp32-camera/blob/v2.0.4/sensors/ov3660.c#L876) uses custom start/end coordinates and line timings for portrait. The [OV2640 driver](https://github.com/espressif/esp32-camera/blob/v2.0.4/sensors/ov2640.c) selects standard SVGA readout through `set_framesize(FRAMESIZE_SVGA)`; this replaces the earlier UXGA portrait path. The user's OV3660 baseline measured about 15 FPS capture/preview before recording; see the recording diagnosis below. The 20 FPS scheduler remains an upper target.

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

If startup detects `Camera sensor PID: 0x0026` but then fails capture validation, the OV2640 was recognized. Startup logs each rejected warm-up frame with missing-frame/format/header/dimension details and capture duration, followed by OV2640 window/output/format register readback. The OV2640 path also explicitly checks the JPEG-format reset because the SDK's window function ignores its internal reset result. A successful startup reports `Camera JPEG confirmed: sensor <encoded width> x <height>, output <visible width> x <height>`. Hardware success of the new aligned capture and adaptation must be confirmed from a fresh startup log and clean stream. Host tests simulate the SDK boundary and cannot establish physical capture or throughput.

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
2. Test both sensor models. Open one viewer and start recording with the button. Confirm `/status` keeps responding, JPEGs grow and the raw stream uses the reported dimensions: portrait for OV3660, landscape for OV2640. A 90° receiver/viewer rotation must swap those dimensions. After swapping sensors, the viewer should use the new source's saved rotation or default to 0°.
3. Record for at least 30 minutes; observe heap/PSRAM, drop counts and SD write times. Export the result using `frames.csv` and check timing/orientation.
4. Disconnect/reconnect Wi-Fi while recording; confirm the same directory keeps receiving frames and preview recovers.
5. Stop/start repeatedly. Check that each finished session has complete JPEGs and a readable frame index.
6. Exercise missing/full SD, unavailable MQTT broker and disconnected gyro. A media failure must be reported without permanently disabling the camera/network or leaking memory across retries.

The FFmpeg concat format was exercised with 600 × 800 sample images: clockwise rotation produces 800 × 600 output and frame timestamp gaps are preserved. The PowerShell wrapper itself was not executed because PowerShell is not installed on this host.

An earlier build was uploaded successfully on **4 October 2026**, at 115200 baud with the SD card removed and the board in download mode. All flashed image hashes verified; flash identification reported 4 MB at 3.3 V. The Serial Monitor had to be closed to release the port. The restored LED logic, stream timeout correction and native portrait changes require a new upload; the user will upload them. On-device dimensions, FPS and soak testing are still required. Sudden power removal can still lose pending frames or filesystem metadata; software cannot make an SD card power-loss-proof.
