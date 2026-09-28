#include "app_shared.h"
#include <PubSubClient.h>
#include <Wire.h>

namespace {
constexpr uint8_t kMpuAddress = 0x68;
constexpr uint8_t kRegPowerMgmt1 = 0x6B;
constexpr uint8_t kRegAccelXoutH = 0x3B;
constexpr uint8_t kRegWhoAmI = 0x75;
constexpr uint8_t kRegGyroConfig = 0x1B;
constexpr uint8_t kRegAccelConfig = 0x1C;

// User-requested pin plan with SD_MMC switched to 1-bit mode.
constexpr int kGyroSdaPin = 3;
constexpr int kGyroSclPin = 13;

constexpr float kGyroScale = 131.0f;
constexpr float kComplementaryAlpha = 0.98f;

constexpr char kMqttBroker[] = "broker.hivemq.com";
constexpr uint16_t kMqttPort = 1883;

WiFiClient sMqttNetClient;
PubSubClient sMqttClient(sMqttNetClient);

bool sImuReady = false;
bool sGyroRunning = false;
bool sGyroHardwareProbed = false;
bool sGyroHardwareDetected = false;

float sGyroXOffset = 0.0f;
float sGyroYOffset = 0.0f;
float sGyroZOffset = 0.0f;

float sYaw = 0.0f;
float sPitch = 0.0f;
float sRoll = 0.0f;

unsigned long sLastSampleMicros = 0;
unsigned long sLastPublishMs = 0;
unsigned long sLastMqttAttemptMs = 0;

String sMqttTopic;
String sMqttClientId;

bool writeMpuRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kMpuAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readMpuRegisters(uint8_t reg, uint8_t* data, size_t len) {
  Wire.beginTransmission(kMpuAddress);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  size_t received = Wire.requestFrom(static_cast<uint8_t>(kMpuAddress), len, true);
  if (received != len) {
    return false;
  }

  for (size_t i = 0; i < len; ++i) {
    data[i] = Wire.read();
  }

  return true;
}

bool readMpuRaw(
  int16_t& ax,
  int16_t& ay,
  int16_t& az,
  int16_t& gx,
  int16_t& gy,
  int16_t& gz
) {
  uint8_t bytes[14];
  if (!readMpuRegisters(kRegAccelXoutH, bytes, sizeof(bytes))) {
    return false;
  }

  ax = static_cast<int16_t>((bytes[0] << 8) | bytes[1]);
  ay = static_cast<int16_t>((bytes[2] << 8) | bytes[3]);
  az = static_cast<int16_t>((bytes[4] << 8) | bytes[5]);
  gx = static_cast<int16_t>((bytes[8] << 8) | bytes[9]);
  gy = static_cast<int16_t>((bytes[10] << 8) | bytes[11]);
  gz = static_cast<int16_t>((bytes[12] << 8) | bytes[13]);

  return true;
}

bool calibrateGyroOffsets() {
  long xSum = 0;
  long ySum = 0;
  long zSum = 0;
  const int sampleCount = 200;

  for (int i = 0; i < sampleCount; ++i) {
    int16_t ax = 0;
    int16_t ay = 0;
    int16_t az = 0;
    int16_t gx = 0;
    int16_t gy = 0;
    int16_t gz = 0;

    if (!readMpuRaw(ax, ay, az, gx, gy, gz)) {
      return false;
    }

    xSum += gx;
    ySum += gy;
    zSum += gz;
    delay(3);
  }

  sGyroXOffset = static_cast<float>(xSum) / static_cast<float>(sampleCount);
  sGyroYOffset = static_cast<float>(ySum) / static_cast<float>(sampleCount);
  sGyroZOffset = static_cast<float>(zSum) / static_cast<float>(sampleCount);
  return true;
}

bool initMpu(String& errorOut) {
  Wire.begin(kGyroSdaPin, kGyroSclPin);
  Wire.setClock(400000UL);

  uint8_t whoAmI = 0;
  if (!readMpuRegisters(kRegWhoAmI, &whoAmI, 1)) {
    errorOut = "Gyro WHO_AM_I read failed on GPIO3/GPIO13";
    return false;
  }

  if ((whoAmI & 0x7E) != 0x68) {
    errorOut = "Unexpected MPU6050 WHO_AM_I value";
    return false;
  }

  sGyroHardwareProbed = true;
  sGyroHardwareDetected = true;

  if (!writeMpuRegister(kRegPowerMgmt1, 0x00)) {
    errorOut = "Unable to wake the MPU6050";
    return false;
  }

  delay(100);
  writeMpuRegister(kRegGyroConfig, 0x00);
  writeMpuRegister(kRegAccelConfig, 0x00);

  if (!calibrateGyroOffsets()) {
    errorOut = "Gyro calibration failed";
    return false;
  }

  sYaw = 0.0f;
  sPitch = 0.0f;
  sRoll = 0.0f;
  sLastSampleMicros = micros();

  Serial.print("Gyro ready on SDA=");
  Serial.print(kGyroSdaPin);
  Serial.print(" SCL=");
  Serial.println(kGyroSclPin);
  errorOut = "";
  return true;
}

void ensureMqttIdentity() {
  String uuid = getGyroIdentityUuid();
  if (uuid.length() == 0) {
    sMqttTopic = "";
    sMqttClientId = "";
    return;
  }

  sMqttTopic = "eyetracker/" + uuid + "/gyro";

  String suffix = uuid;
  if (suffix.length() > 18) {
    suffix = suffix.substring(0, 18);
  }

  sMqttClientId = "espcam-gyro-" + suffix;
}

void ensureMqttConnected() {
  if (sMqttTopic.length() == 0 || sMqttClientId.length() == 0) {
    ensureMqttIdentity();
  }

  if (sMqttTopic.length() == 0 || sMqttClientId.length() == 0 || sMqttClient.connected()) {
    return;
  }

  unsigned long now = millis();
  if (now - sLastMqttAttemptMs < 3000) {
    return;
  }

  sLastMqttAttemptMs = now;
  if (sMqttClient.connect(sMqttClientId.c_str())) {
    Serial.println("Gyro MQTT connected");
  }
}

bool updateOrientation() {
  int16_t ax = 0;
  int16_t ay = 0;
  int16_t az = 0;
  int16_t gx = 0;
  int16_t gy = 0;
  int16_t gz = 0;

  if (!readMpuRaw(ax, ay, az, gx, gy, gz)) {
    return false;
  }

  unsigned long nowMicros = micros();
  float dt = static_cast<float>(nowMicros - sLastSampleMicros) / 1000000.0f;
  sLastSampleMicros = nowMicros;

  if (dt <= 0.0f || dt > 1.0f) {
    return false;
  }

  float gxRate = (static_cast<float>(gx) - sGyroXOffset) / kGyroScale;
  float gyRate = (static_cast<float>(gy) - sGyroYOffset) / kGyroScale;
  float gzRate = (static_cast<float>(gz) - sGyroZOffset) / kGyroScale;

  float axf = static_cast<float>(ax);
  float ayf = static_cast<float>(ay);
  float azf = static_cast<float>(az);

  float accelPitch = atan2f(ayf, sqrtf(axf * axf + azf * azf)) * 180.0f / PI;
  float accelRoll = atan2f(-axf, azf) * 180.0f / PI;

  sPitch = kComplementaryAlpha * (sPitch + gxRate * dt) + (1.0f - kComplementaryAlpha) * accelPitch;
  sRoll = kComplementaryAlpha * (sRoll + gyRate * dt) + (1.0f - kComplementaryAlpha) * accelRoll;
  sYaw += gzRate * dt;

  return true;
}
}  // namespace

bool gyroHardwareAvailable() {
  if (sGyroHardwareProbed) {
    return sGyroHardwareDetected;
  }

  Wire.begin(kGyroSdaPin, kGyroSclPin);
  Wire.setClock(400000UL);

  uint8_t whoAmI = 0;
  sGyroHardwareDetected = readMpuRegisters(kRegWhoAmI, &whoAmI, 1) && ((whoAmI & 0x7E) == 0x68);
  sGyroHardwareProbed = true;
  return sGyroHardwareDetected;
}

bool gyroValidateRuntime(String& messageOut) {
  messageOut = "";

  if (!isGyroToggleEnabled()) {
    gyroStop();
    return true;
  }

  if (getGyroIdentityUuid().length() == 0) {
    gyroStop();
    messageOut = "Gyro UUID is missing";
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    gyroStop();
    messageOut = "Wi-Fi is not connected";
    return false;
  }

  if (!gyroHardwareAvailable()) {
    gyroStop();
    messageOut = "Gyro hardware is not available on GPIO3/GPIO13";
    return false;
  }

  if (!sImuReady) {
    if (!initMpu(messageOut)) {
      gyroStop();
      return false;
    }
    sImuReady = true;
  }

  ensureMqttIdentity();
  sMqttClient.setServer(kMqttBroker, kMqttPort);
  sGyroRunning = true;
  return true;
}

bool gyroBeginIfEligible() {
  String ignored;
  return gyroValidateRuntime(ignored);
}

void gyroStop() {
  if (sMqttClient.connected()) {
    sMqttClient.disconnect();
  }

  sGyroRunning = false;
}

bool gyroIsRunning() {
  return sGyroRunning;
}

void gyroLoop() {
  if (!isGyroToggleEnabled() || getGyroIdentityUuid().length() == 0) {
    gyroStop();
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    gyroStop();
    return;
  }

  if (!sGyroRunning) {
    gyroBeginIfEligible();
  }

  if (!sGyroRunning) {
    return;
  }

  unsigned long nowMs = millis();
  if (nowMs - sLastPublishMs < 50) {
    if (sMqttClient.connected()) {
      sMqttClient.loop();
    }
    return;
  }

  if (!updateOrientation()) {
    return;
  }

  ensureMqttConnected();
  if (sMqttClient.connected()) {
    sMqttClient.loop();

    char payload[48];
    snprintf(payload, sizeof(payload), "%.1f,%.1f,%.1f", sYaw, sPitch, sRoll);
    sMqttClient.publish(sMqttTopic.c_str(), payload);
  }

  sLastPublishMs = nowMs;
}
