#include "slave_app.h"

const uint8_t kSlaveLedPin = 2;
const uint8_t kSlaveButtonPin = 15;
const uint8_t kSlaveGyroSdaPin = 5;
const uint8_t kSlaveGyroSclPin = 4;
const unsigned long kSlaveButtonDebounceMs = 50;
const unsigned long kSlaveStatusIntervalMs = 1000;
const unsigned long kSlaveHelloIntervalMs = 2000;
const unsigned long kSlaveSlowBlinkMs = 500;
const unsigned long kSlaveFastBlinkMs = 100;

SlaveConfig slaveConfig;
SlaveRuntimeState slaveRuntime;
SlaveTelemetry slaveTelemetry;
WiFiClient slaveMqttNet;
PubSubClient slaveMqttClient(slaveMqttNet);

namespace {
constexpr char kFirmwareVersion[] = "esp12e-slave-v1";

bool shouldSlaveWifiConnect() {
  return slaveRuntime.wifiEnable && slaveConfig.received && slaveConfig.wifiSsid.length() > 0;
}
}  // namespace

void processSlaveWifi() {
  const bool shouldConnect = shouldSlaveWifiConnect();

  if (!shouldConnect) {
    if (WiFi.getMode() != WIFI_OFF) {
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    }
    slaveTelemetry.wifiConnected = false;
    slaveTelemetry.mqttConnected = false;
    return;
  }

  if (WiFi.getMode() != WIFI_STA) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(slaveConfig.wifiSsid.c_str(), slaveConfig.wifiPassword.c_str());
  } else if (WiFi.status() != WL_CONNECTED && WiFi.SSID() != slaveConfig.wifiSsid) {
    WiFi.disconnect();
    WiFi.begin(slaveConfig.wifiSsid.c_str(), slaveConfig.wifiPassword.c_str());
  }

  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  if (slaveTelemetry.wifiConnected != wifiConnected) {
    slaveTelemetry.wifiConnected = wifiConnected;
    markSlaveStatusDirty();
  }
}

void processSlaveButton() {
  const bool physicalPressed = digitalRead(kSlaveButtonPin) == HIGH;

  if (physicalPressed != slaveTelemetry.buttonLastPhysicalPressed) {
    slaveTelemetry.buttonLastPhysicalPressed = physicalPressed;
    slaveTelemetry.buttonLastDebounceAt = millis();
  }

  if (millis() - slaveTelemetry.buttonLastDebounceAt < kSlaveButtonDebounceMs) {
    return;
  }

  if (slaveTelemetry.buttonStablePressed == physicalPressed) {
    return;
  }

  slaveTelemetry.buttonStablePressed = physicalPressed;
  if (slaveTelemetry.buttonStablePressed) {
    Serial.println("SC1 EVENT type=button_click");
  }
}

void applySlaveLedMode() {
  bool ledOn = false;

  if (slaveRuntime.ledMode == "recording" || slaveRuntime.ledMode == "success") {
    ledOn = true;
  } else if (slaveRuntime.ledMode == "connecting") {
    ledOn = ((millis() / kSlaveSlowBlinkMs) % 2) == 0;
  } else if (slaveRuntime.ledMode == "error") {
    ledOn = ((millis() / kSlaveFastBlinkMs) % 2) == 0;
  }

  digitalWrite(kSlaveLedPin, ledOn ? HIGH : LOW);
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);

  pinMode(kSlaveLedPin, OUTPUT);
  digitalWrite(kSlaveLedPin, LOW);

  // GPIO15 (NodeMCU D8) must stay LOW at boot, so the button should drive it
  // HIGH only when pressed.
  pinMode(kSlaveButtonPin, INPUT);

  initGyroRuntime();
  slaveTelemetry.gyroPresent = probeGyroHardware();
  slaveTelemetry.lastErrorType = "none";
  slaveTelemetry.lastErrorMessage = "";

  initSerialProtocol();
  markSlaveStatusDirty();

  (void)kFirmwareVersion;
}

void loop() {
  processSerialProtocol();
  processSlaveWifi();
  processGyroRuntime();
  processSlaveButton();
  applySlaveLedMode();
  delay(1);
}
