#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>

struct SlaveConfig {
  bool received = false;
  bool gyroEnabled = false;
  String wifiSsid;
  String wifiPassword;
  String validationRoute;
  String gyroUuid;
  String mqttHost = "broker.hivemq.com";
  uint16_t mqttPort = 1883;
  String mqttTopic;
};

struct SlaveRuntimeState {
  bool wifiEnable = false;
  bool recordingActive = false;
  String ledMode = "off";
};

struct SlaveTelemetry {
  bool gyroPresent = false;
  bool gyroReady = false;
  bool wifiConnected = false;
  bool mqttConnected = false;
  bool configReady = false;
  bool runtimeReady = false;
  bool buttonStablePressed = false;
  bool buttonLastPhysicalPressed = false;
  unsigned long buttonLastDebounceAt = 0;
  unsigned long lastStatusSentAt = 0;
  unsigned long lastHelloSentAt = 0;
  unsigned long lastMqttAttemptAt = 0;
  String lastErrorType = "none";
  String lastErrorMessage;
};

extern const uint8_t kSlaveLedPin;
extern const uint8_t kSlaveButtonPin;
extern const uint8_t kSlaveGyroSdaPin;
extern const uint8_t kSlaveGyroSclPin;
extern const unsigned long kSlaveButtonDebounceMs;
extern const unsigned long kSlaveStatusIntervalMs;
extern const unsigned long kSlaveHelloIntervalMs;
extern const unsigned long kSlaveSlowBlinkMs;
extern const unsigned long kSlaveFastBlinkMs;

extern SlaveConfig slaveConfig;
extern SlaveRuntimeState slaveRuntime;
extern SlaveTelemetry slaveTelemetry;
extern WiFiClient slaveMqttNet;
extern PubSubClient slaveMqttClient;

void initSerialProtocol();
void processSerialProtocol();
void markSlaveStatusDirty();

void initGyroRuntime();
bool probeGyroHardware();
bool ensureGyroReady();
void processGyroRuntime();
void stopGyroRuntime();

void processSlaveWifi();
void processSlaveButton();
void applySlaveLedMode();
