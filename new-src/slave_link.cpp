#include "app_shared.h"

namespace {
HardwareSerial slaveSerial(1);
String slaveRxLine;

constexpr int kSlaveRxPin = 3;
constexpr int kSlaveTxPin = 1;
constexpr unsigned long kSlaveBootDelayMs = 2000;
constexpr unsigned long kSlaveTimeoutMs = 6000;
constexpr unsigned long kSlaveConfigRetryMs = 3000;
constexpr unsigned long kSlaveRuntimeRetryMs = 1000;
constexpr char kProtocolPrefix[] = "SC1 ";
constexpr char kMqttBrokerHost[] = "broker.hivemq.com";
constexpr uint16_t kMqttBrokerPort = 1883;

String protocolEncode(const String& value) {
  String encoded;
  encoded.reserve(value.length() * 3);

  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    const bool safe =
      (c >= 'a' && c <= 'z') ||
      (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9') ||
      c == '-' || c == '_' || c == '.' || c == '~';

    if (safe) {
      encoded += c;
      continue;
    }

    char buffer[4];
    snprintf(buffer, sizeof(buffer), "%%%02X", static_cast<unsigned char>(c));
    encoded += buffer;
  }

  return encoded;
}

String protocolDecode(const String& value) {
  String decoded;
  decoded.reserve(value.length());

  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (c == '%' && i + 2 < value.length()) {
      const char hex[3] = {value[i + 1], value[i + 2], '\0'};
      decoded += static_cast<char>(strtol(hex, nullptr, 16));
      i += 2;
      continue;
    }

    decoded += c;
  }

  return decoded;
}

String commandField(const String& line, const char* key) {
  const String pattern = String(key) + "=";
  const int start = line.indexOf(pattern);
  if (start < 0) {
    return "";
  }

  const int valueStart = start + pattern.length();
  int valueEnd = line.indexOf(' ', valueStart);
  if (valueEnd < 0) {
    valueEnd = line.length();
  }

  return protocolDecode(line.substring(valueStart, valueEnd));
}

bool commandFieldBool(const String& line, const char* key) {
  const String value = commandField(line, key);
  return value == "1" || value.equalsIgnoreCase("true") || value.equalsIgnoreCase("yes");
}

void sendCommand(const String& line) {
  slaveSerial.print(kProtocolPrefix);
  slaveSerial.print(line);
  slaveSerial.print('\n');
}

String buildGyroTopic() {
  const String uuid = getGyroIdentityUuid();
  if (uuid.length() == 0) {
    return "";
  }

  return "eyetracker/" + uuid + "/gyro";
}

bool shouldEnableSlaveWifi() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  return runtimeMode == RuntimeMode::PostConnectValidation ||
         runtimeMode == RuntimeMode::ConnectedIdle ||
         runtimeMode == RuntimeMode::Recording;
}

void markSlaveDisconnected() {
  slaveState.present = false;
  slaveState.helloReceived = false;
  slaveState.configReady = false;
  slaveState.runtimeReady = false;
  slaveState.gyroPresent = false;
  slaveState.gyroReady = false;
  slaveState.wifiConnected = false;
  slaveState.mqttConnected = false;
  slaveState.firmwareVersion = "";
  slaveState.lastErrorType = "disconnect";
  slaveState.lastErrorMessage = "Slave ESP8266 connection timed out";
  slaveState.configDirty = true;
  slaveState.runtimeDirty = true;
}

void handleHelloCommand(const String& line) {
  slaveState.present = true;
  slaveState.helloReceived = true;
  slaveState.lastSeenAt = millis();
  slaveState.firmwareVersion = commandField(line, "fw");
  slaveState.gyroPresent = commandFieldBool(line, "gyro_present");
  slaveState.configReady = false;
  slaveState.runtimeReady = false;
  slaveState.configDirty = true;
  slaveState.runtimeDirty = true;
}

void handleStatusCommand(const String& line) {
  slaveState.present = true;
  slaveState.lastSeenAt = millis();
  slaveState.gyroPresent = commandFieldBool(line, "gyro_present");
  slaveState.gyroReady = commandFieldBool(line, "gyro_ready");
  slaveState.wifiConnected = commandFieldBool(line, "wifi_connected");
  slaveState.mqttConnected = commandFieldBool(line, "mqtt_connected");
  slaveState.configReady = commandFieldBool(line, "config_ready");
  slaveState.runtimeReady = commandFieldBool(line, "runtime_ready");
  slaveState.lastErrorType = commandField(line, "last_error_type");
  slaveState.lastErrorMessage = commandField(line, "last_error_message");
}

void handleEventCommand(const String& line) {
  if (commandField(line, "type") == "button_click") {
    slaveState.buttonClickPending = true;
  }
}

void handleSlaveLine(const String& line) {
  if (!line.startsWith(kProtocolPrefix)) {
    return;
  }

  String payload = line.substring(strlen(kProtocolPrefix));
  const int firstSpace = payload.indexOf(' ');
  const String command = (firstSpace < 0) ? payload : payload.substring(0, firstSpace);

  if (command == "HELLO") {
    handleHelloCommand(payload);
  } else if (command == "STATUS") {
    handleStatusCommand(payload);
  } else if (command == "EVENT") {
    handleEventCommand(payload);
  }
}

void readSlaveInput() {
  while (slaveSerial.available() > 0) {
    const char c = static_cast<char>(slaveSerial.read());
    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      if (slaveRxLine.length() > 0) {
        handleSlaveLine(slaveRxLine);
        slaveRxLine = "";
      }
      continue;
    }

    if (slaveRxLine.length() < 384) {
      slaveRxLine += c;
    } else {
      slaveRxLine = "";
    }
  }
}

void sendSlaveConfigIfNeeded() {
  if (!slaveState.present || millis() < slaveState.syncNotBeforeAt) {
    return;
  }

  const bool retryDue = !slaveState.configReady && (millis() - slaveState.lastConfigSentAt >= kSlaveConfigRetryMs);
  if (!slaveState.configDirty && !retryDue) {
    return;
  }

  String command = "CONFIG";
  command += " wifi_ssid=" + protocolEncode(savedSsidCache);
  command += " wifi_pass=" + protocolEncode(savedPasswordCache);
  command += " gyro_enabled=" + String(isGyroToggleEnabled() ? "1" : "0");
  command += " validation_route=" + protocolEncode(getGyroValidationRoute());
  command += " gyro_uuid=" + protocolEncode(getGyroIdentityUuid());
  command += " mqtt_host=" + protocolEncode(String(kMqttBrokerHost));
  command += " mqtt_port=" + String(kMqttBrokerPort);
  command += " mqtt_topic=" + protocolEncode(buildGyroTopic());

  sendCommand(command);
  slaveState.lastConfigSentAt = millis();
  slaveState.configDirty = false;
}

void sendSlaveRuntimeIfNeeded() {
  if (!slaveState.present || millis() < slaveState.syncNotBeforeAt) {
    return;
  }

  const bool retryDue = !slaveState.runtimeReady && (millis() - slaveState.lastRuntimeSentAt >= kSlaveRuntimeRetryMs);
  if (!slaveState.runtimeDirty && !retryDue) {
    return;
  }

  String command = "RUNTIME";
  command += " wifi_enable=" + String(shouldEnableSlaveWifi() ? "1" : "0");
  command += " recording=" + String(recordingSession.active ? "1" : "0");
  command += " led_mode=" + protocolEncode(currentSlaveLedMode());

  sendCommand(command);
  slaveState.lastRuntimeSentAt = millis();
  slaveState.runtimeDirty = false;
}
}  // namespace

void initSlaveLink() {
  slaveSerial.begin(115200, SERIAL_8N1, kSlaveRxPin, kSlaveTxPin);
  slaveState.syncNotBeforeAt = millis() + kSlaveBootDelayMs;
  slaveState.configDirty = true;
  slaveState.runtimeDirty = true;
}

void processSlaveLink() {
  readSlaveInput();

  if (slaveState.present && millis() - slaveState.lastSeenAt >= kSlaveTimeoutMs) {
    markSlaveDisconnected();
  }

  sendSlaveConfigIfNeeded();
  sendSlaveRuntimeIfNeeded();
}

void notifySlaveConfigChanged() {
  slaveState.configDirty = true;
  slaveState.configReady = false;
}

void notifySlaveRuntimeChanged() {
  slaveState.runtimeDirty = true;
  slaveState.runtimeReady = false;
}

String currentSlaveLedMode() {
  switch (runtimeMode) {
    case RuntimeMode::WifiConnecting:
      return "connecting";
    case RuntimeMode::PostConnectValidation:
      return "success";
    case RuntimeMode::Recording:
      return "recording";
    case RuntimeMode::ErrorFallback:
      return "error";
    case RuntimeMode::ConnectedIdle:
    case RuntimeMode::SoftApConfig:
    default:
      return "off";
  }
}
