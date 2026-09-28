#include "slave_app.h"

namespace {
constexpr uint8_t kMpuAddress = 0x68;
constexpr uint8_t kRegPowerMgmt1 = 0x6B;
constexpr uint8_t kRegAccelXoutH = 0x3B;
constexpr uint8_t kRegWhoAmI = 0x75;
constexpr uint8_t kRegGyroConfig = 0x1B;
constexpr uint8_t kRegAccelConfig = 0x1C;
constexpr float kGyroScale = 131.0f;
constexpr float kComplementaryAlpha = 0.98f;

bool imuInitialized = false;
float gyroXOffset = 0.0f;
float gyroYOffset = 0.0f;
float gyroZOffset = 0.0f;
float yawAngle = 0.0f;
float pitchAngle = 0.0f;
float rollAngle = 0.0f;
unsigned long lastSampleMicros = 0;
unsigned long lastPublishMs = 0;

void setGyroError(const String& type, const String& message) {
  slaveTelemetry.lastErrorType = type;
  slaveTelemetry.lastErrorMessage = message;
  markSlaveStatusDirty();
}

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

  const size_t received = Wire.requestFrom(static_cast<uint8_t>(kMpuAddress), len, true);
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

  gyroXOffset = static_cast<float>(xSum) / static_cast<float>(sampleCount);
  gyroYOffset = static_cast<float>(ySum) / static_cast<float>(sampleCount);
  gyroZOffset = static_cast<float>(zSum) / static_cast<float>(sampleCount);
  return true;
}

bool initMpu() {
  uint8_t whoAmI = 0;
  if (!readMpuRegisters(kRegWhoAmI, &whoAmI, 1)) {
    setGyroError("gyro", "Gyro WHO_AM_I read failed on GPIO5/GPIO4");
    return false;
  }

  if ((whoAmI & 0x7E) != 0x68) {
    setGyroError("gyro", "Unexpected gyroscope WHO_AM_I value");
    return false;
  }

  if (!writeMpuRegister(kRegPowerMgmt1, 0x00)) {
    setGyroError("gyro", "Unable to wake the gyroscope");
    return false;
  }

  delay(100);
  writeMpuRegister(kRegGyroConfig, 0x00);
  writeMpuRegister(kRegAccelConfig, 0x00);

  if (!calibrateGyroOffsets()) {
    setGyroError("gyro", "Gyro calibration failed");
    return false;
  }

  yawAngle = 0.0f;
  pitchAngle = 0.0f;
  rollAngle = 0.0f;
  lastSampleMicros = micros();
  slaveTelemetry.lastErrorType = "none";
  slaveTelemetry.lastErrorMessage = "";
  return true;
}

bool updateOrientation() {
  int16_t ax = 0;
  int16_t ay = 0;
  int16_t az = 0;
  int16_t gx = 0;
  int16_t gy = 0;
  int16_t gz = 0;

  if (!readMpuRaw(ax, ay, az, gx, gy, gz)) {
    setGyroError("gyro", "Gyro sample read failed");
    return false;
  }

  const unsigned long nowMicros = micros();
  const float dt = static_cast<float>(nowMicros - lastSampleMicros) / 1000000.0f;
  lastSampleMicros = nowMicros;

  if (dt <= 0.0f || dt > 1.0f) {
    return false;
  }

  const float gxRate = (static_cast<float>(gx) - gyroXOffset) / kGyroScale;
  const float gyRate = (static_cast<float>(gy) - gyroYOffset) / kGyroScale;
  const float gzRate = (static_cast<float>(gz) - gyroZOffset) / kGyroScale;

  const float axf = static_cast<float>(ax);
  const float ayf = static_cast<float>(ay);
  const float azf = static_cast<float>(az);

  const float accelPitch = atan2f(ayf, sqrtf(axf * axf + azf * azf)) * 180.0f / PI;
  const float accelRoll = atan2f(-axf, azf) * 180.0f / PI;

  pitchAngle = kComplementaryAlpha * (pitchAngle + gxRate * dt) + (1.0f - kComplementaryAlpha) * accelPitch;
  rollAngle = kComplementaryAlpha * (rollAngle + gyRate * dt) + (1.0f - kComplementaryAlpha) * accelRoll;
  yawAngle += gzRate * dt;
  return true;
}

void ensureMqttConnected() {
  if (slaveConfig.mqttTopic.length() == 0 || slaveConfig.gyroUuid.length() == 0) {
    slaveTelemetry.mqttConnected = false;
    return;
  }

  if (slaveMqttClient.connected()) {
    slaveTelemetry.mqttConnected = true;
    return;
  }

  if (millis() - slaveTelemetry.lastMqttAttemptAt < 3000) {
    return;
  }

  slaveTelemetry.lastMqttAttemptAt = millis();
  slaveMqttClient.setServer(slaveConfig.mqttHost.c_str(), slaveConfig.mqttPort);

  String clientId = "esp12e-gyro-";
  clientId += slaveConfig.gyroUuid.substring(0, min(static_cast<size_t>(18), slaveConfig.gyroUuid.length()));

  if (slaveMqttClient.connect(clientId.c_str())) {
    slaveTelemetry.mqttConnected = true;
    slaveTelemetry.lastErrorType = "none";
    slaveTelemetry.lastErrorMessage = "";
    markSlaveStatusDirty();
  } else {
    slaveTelemetry.mqttConnected = false;
    setGyroError("mqtt", "MQTT connection failed");
  }
}
}  // namespace

void initGyroRuntime() {
  Wire.begin(kSlaveGyroSdaPin, kSlaveGyroSclPin);
  Wire.setClock(400000UL);
}

bool probeGyroHardware() {
  uint8_t whoAmI = 0;
  return readMpuRegisters(kRegWhoAmI, &whoAmI, 1) && ((whoAmI & 0x7E) == 0x68);
}

bool ensureGyroReady() {
  if (!slaveTelemetry.gyroPresent) {
    setGyroError("gyro", "Gyro hardware is not available on GPIO5/GPIO4");
    return false;
  }

  if (imuInitialized) {
    return true;
  }

  if (!initMpu()) {
    slaveTelemetry.gyroReady = false;
    return false;
  }

  imuInitialized = true;
  slaveTelemetry.gyroReady = true;
  markSlaveStatusDirty();
  return true;
}

void stopGyroRuntime() {
  const bool changed = slaveTelemetry.gyroReady || slaveTelemetry.mqttConnected || imuInitialized;

  if (slaveMqttClient.connected()) {
    slaveMqttClient.disconnect();
  }

  slaveTelemetry.gyroReady = false;
  slaveTelemetry.mqttConnected = false;
  imuInitialized = false;
  if (changed) {
    markSlaveStatusDirty();
  }
}

void processGyroRuntime() {
  if (!slaveConfig.received || !slaveRuntime.wifiEnable || !slaveConfig.gyroEnabled) {
    stopGyroRuntime();
    return;
  }

  if (!slaveTelemetry.wifiConnected) {
    slaveTelemetry.mqttConnected = false;
    slaveTelemetry.gyroReady = false;
    return;
  }

  slaveTelemetry.gyroPresent = probeGyroHardware();
  if (!ensureGyroReady()) {
    return;
  }

  ensureMqttConnected();
  if (!slaveMqttClient.connected()) {
    return;
  }

  slaveMqttClient.loop();
  if (millis() - lastPublishMs < 50) {
    return;
  }

  if (!updateOrientation()) {
    return;
  }

  char payload[48];
  snprintf(payload, sizeof(payload), "%.1f,%.1f,%.1f", yawAngle, pitchAngle, rollAngle);
  slaveMqttClient.publish(slaveConfig.mqttTopic.c_str(), payload);
  lastPublishMs = millis();
}
