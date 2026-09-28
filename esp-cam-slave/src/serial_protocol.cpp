#include "slave_app.h"

namespace {
String serialRxLine;
constexpr char kProtocolPrefix[] = "SC1 ";
constexpr char kFirmwareVersion[] = "esp12e-slave-v1";

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

void sendHelloIfDue() {
  if (millis() - slaveTelemetry.lastHelloSentAt < kSlaveHelloIntervalMs) {
    return;
  }

  slaveTelemetry.lastHelloSentAt = millis();

  String line = "SC1 HELLO role=slave";
  line += " fw=" + String(kFirmwareVersion);
  line += " gyro_present=" + String(slaveTelemetry.gyroPresent ? "1" : "0");
  line += " led_pin=2";
  line += " button_pin=15";
  line += " gyro_sda=5";
  line += " gyro_scl=4";
  Serial.println(line);
}

void sendStatusIfDue() {
  const bool due = millis() - slaveTelemetry.lastStatusSentAt >= kSlaveStatusIntervalMs;
  if (!due) {
    return;
  }

  slaveTelemetry.lastStatusSentAt = millis();

  String line = "SC1 STATUS";
  line += " gyro_present=" + String(slaveTelemetry.gyroPresent ? "1" : "0");
  line += " gyro_ready=" + String(slaveTelemetry.gyroReady ? "1" : "0");
  line += " wifi_connected=" + String(slaveTelemetry.wifiConnected ? "1" : "0");
  line += " mqtt_connected=" + String(slaveTelemetry.mqttConnected ? "1" : "0");
  line += " config_ready=" + String(slaveTelemetry.configReady ? "1" : "0");
  line += " runtime_ready=" + String(slaveTelemetry.runtimeReady ? "1" : "0");
  line += " last_error_type=" + protocolEncode(slaveTelemetry.lastErrorType);
  line += " last_error_message=" + protocolEncode(slaveTelemetry.lastErrorMessage);
  Serial.println(line);
}

void applyConfigCommand(const String& payload) {
  slaveConfig.wifiSsid = commandField(payload, "wifi_ssid");
  slaveConfig.wifiPassword = commandField(payload, "wifi_pass");
  slaveConfig.gyroEnabled = commandFieldBool(payload, "gyro_enabled");
  slaveConfig.validationRoute = commandField(payload, "validation_route");
  slaveConfig.gyroUuid = commandField(payload, "gyro_uuid");
  slaveConfig.mqttHost = commandField(payload, "mqtt_host");
  const String mqttPort = commandField(payload, "mqtt_port");
  slaveConfig.mqttPort = mqttPort.length() > 0 ? static_cast<uint16_t>(mqttPort.toInt()) : 1883;
  slaveConfig.mqttTopic = commandField(payload, "mqtt_topic");
  slaveConfig.received = true;

  slaveTelemetry.configReady = true;
  slaveTelemetry.lastErrorType = "none";
  slaveTelemetry.lastErrorMessage = "";
  slaveTelemetry.lastStatusSentAt = 0;

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  stopGyroRuntime();
}

void applyRuntimeCommand(const String& payload) {
  slaveRuntime.wifiEnable = commandFieldBool(payload, "wifi_enable");
  slaveRuntime.recordingActive = commandFieldBool(payload, "recording");
  slaveRuntime.ledMode = commandField(payload, "led_mode");
  if (slaveRuntime.ledMode.length() == 0) {
    slaveRuntime.ledMode = "off";
  }

  slaveTelemetry.runtimeReady = true;
  slaveTelemetry.lastStatusSentAt = 0;

  if (!slaveRuntime.wifiEnable) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    stopGyroRuntime();
  }
}

void handleProtocolLine(const String& line) {
  if (!line.startsWith(kProtocolPrefix)) {
    return;
  }

  const String payload = line.substring(strlen(kProtocolPrefix));
  const int firstSpace = payload.indexOf(' ');
  const String command = (firstSpace < 0) ? payload : payload.substring(0, firstSpace);

  if (command == "CONFIG") {
    applyConfigCommand(payload);
  } else if (command == "RUNTIME") {
    applyRuntimeCommand(payload);
  }
}
}  // namespace

void initSerialProtocol() {
  serialRxLine = "";
  slaveTelemetry.lastHelloSentAt = 0;
  slaveTelemetry.lastStatusSentAt = 0;
}

void processSerialProtocol() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      if (serialRxLine.length() > 0) {
        handleProtocolLine(serialRxLine);
        serialRxLine = "";
      }
      continue;
    }

    if (serialRxLine.length() < 384) {
      serialRxLine += c;
    } else {
      serialRxLine = "";
    }
  }

  sendHelloIfDue();
  sendStatusIfDue();
}

void markSlaveStatusDirty() {
  slaveTelemetry.lastStatusSentAt = 0;
}
