# ESP32-CAM <-> ESP12E Serial Protocol

This project now uses two boards:

- `ESP32-CAM` as the master
- `ESP12E / ESP8266` as the slave

The master owns:

- Wi-Fi setup and persistence
- web UI and routes
- camera streaming
- SD recording
- session naming and status reporting

The slave owns:

- button input
- LED output
- gyro I2C handling
- gyro MQTT publishing

The slave does not store configuration permanently. The master always sends the current configuration after boot and whenever it changes.

## Wiring

### Master ESP32-CAM

- `TX GPIO1` -> slave `RX GPIO3`
- `RX GPIO3` <- slave `TX GPIO1`
- `GND` -> slave `GND`

### Slave ESP12E

- `GPIO2` -> LED positive
- `GND` -> LED negative
- `GPIO15` -> button
- button other side -> `3.3V`

`GPIO15` is a boot strap pin on ESP8266, so the slave expects the button to drive it `HIGH` only when pressed.

### Gyro on Slave

- `GPIO5` (`D1`) -> I2C SDA
- `GPIO4` (`D2`) -> I2C SCL

## Boot and Sync Flow

1. The slave boots and starts sending `HELLO` and `STATUS`.
2. The master waits `2 seconds` before sending sync data.
3. The master sends the current saved configuration to the slave with `CONFIG`.
4. The master sends current runtime state with `RUNTIME`.
5. The slave applies that state in RAM only.
6. If runtime allows Wi-Fi and gyro is enabled, the slave connects Wi-Fi and starts gyro MQTT publishing.

## Framing

Each command is one text line terminated by `\n`.

Every valid protocol line starts with:

```text
SC1
```

Everything else on UART is ignored. This lets both boards survive ROM boot messages or manual keyboard input noise.

## Encoding

Field values use percent encoding for spaces and special characters.

Examples:

- space -> `%20`
- slash -> `%2F`
- colon -> `%3A`

## Master -> Slave Commands

### `CONFIG`

Purpose:

- send all non-persistent runtime configuration to the slave

Fields:

- `wifi_ssid`
- `wifi_pass`
- `gyro_enabled`
- `validation_route`
- `gyro_uuid`
- `mqtt_host`
- `mqtt_port`
- `mqtt_topic`

Example:

```text
SC1 CONFIG wifi_ssid=MyWiFi wifi_pass=Secret123 gyro_enabled=1 validation_route=https%3A%2F%2Fexample.com%2Fapi%2Fgaze%2Fvalidate%2Fuuid gyro_uuid=device-abc mqtt_host=broker.hivemq.com mqtt_port=1883 mqtt_topic=eyetracker%2Fdevice-abc%2Fgyro
```

Slave behavior:

- replace the in-memory config
- mark `config_ready=1`
- drop any old Wi-Fi session
- wait for `RUNTIME` to decide whether Wi-Fi should actually be active

### `RUNTIME`

Purpose:

- send current operational state to the slave

Fields:

- `wifi_enable`
- `recording`
- `led_mode`

Example:

```text
SC1 RUNTIME wifi_enable=1 recording=0 led_mode=success
```

`led_mode` values:

- `off`
- `connecting`
- `success`
- `error`
- `recording`

Slave behavior:

- apply LED behavior immediately
- if `wifi_enable=1`, connect to Wi-Fi using the last `CONFIG`
- if `wifi_enable=0`, disconnect Wi-Fi and stop gyro publishing

## Slave -> Master Commands

### `HELLO`

Purpose:

- announce that the slave is online

Fields:

- `role`
- `fw`
- `gyro_present`
- `led_pin`
- `button_pin`
- `gyro_sda`
- `gyro_scl`

Example:

```text
SC1 HELLO role=slave fw=esp12e-slave-v1 gyro_present=1 led_pin=2 button_pin=15 gyro_sda=5 gyro_scl=4
```

Master behavior:

- mark the slave as present
- schedule a fresh `CONFIG`
- schedule a fresh `RUNTIME`

### `STATUS`

Purpose:

- periodic health/status snapshot from the slave

Fields:

- `gyro_present`
- `gyro_ready`
- `wifi_connected`
- `mqtt_connected`
- `config_ready`
- `runtime_ready`
- `last_error_type`
- `last_error_message`

Example:

```text
SC1 STATUS gyro_present=1 gyro_ready=1 wifi_connected=1 mqtt_connected=1 config_ready=1 runtime_ready=1 last_error_type=none last_error_message=
```

Master behavior:

- update `/status`
- drive gyro validation decisions during the post-connect success window

### `EVENT`

Purpose:

- notify the master about button actions

Currently supported:

- `type=button_click`

Example:

```text
SC1 EVENT type=button_click
```

Master behavior:

- first click starts recording
- second click stops recording

## Manual Testing With Keyboard Input

You can simulate either side from a serial terminal by typing full protocol lines followed by Enter.

### Simulate Slave -> Master

Example button click:

```text
SC1 EVENT type=button_click
```

Example ready status:

```text
SC1 STATUS gyro_present=1 gyro_ready=1 wifi_connected=1 mqtt_connected=1 config_ready=1 runtime_ready=1 last_error_type=none last_error_message=
```

### Simulate Master -> Slave

Example config:

```text
SC1 CONFIG wifi_ssid=TestNet wifi_pass=Pass1234 gyro_enabled=1 validation_route=https%3A%2F%2Fexample.com%2Fapi%2Fgaze%2Fvalidate%2Fuuid gyro_uuid=test-uuid mqtt_host=broker.hivemq.com mqtt_port=1883 mqtt_topic=eyetracker%2Ftest-uuid%2Fgyro
```

Example runtime:

```text
SC1 RUNTIME wifi_enable=1 recording=0 led_mode=connecting
```

## PlatformIO Dependencies

### Master `ESP32-CAM`

- `espressif/esp32-camera`
- `knolleary/PubSubClient`
- `bitbank2/JPEGENC`

### Slave `ESP12E`

- `knolleary/PubSubClient`
- built-in Arduino ESP8266 libraries:
  - `ESP8266WiFi`
  - `Wire`

## Notes

- The slave keeps no permanent settings.
- The master is the source of truth for Wi-Fi and gyro configuration.
- The slave button is debounced locally, so the master receives clean click events only.
- The master still ignores any non-`SC1` lines, which helps when the boards emit boot chatter on UART.
