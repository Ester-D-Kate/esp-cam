#include "app_shared.h"

bool gyroHardwareAvailable() {
  return slaveState.present && slaveState.gyroPresent;
}

bool gyroValidateRuntime(String& messageOut) {
  messageOut = "";

  if (!isGyroToggleEnabled()) {
    return true;
  }

  if (!slaveState.present) {
    messageOut = "Slave ESP8266 is not connected";
    return false;
  }

  if (!slaveState.gyroPresent) {
    messageOut = "Gyro hardware is not available on the ESP8266 slave";
    return false;
  }

  if (getGyroIdentityUuid().length() == 0) {
    messageOut = "Gyro UUID is missing";
    return false;
  }

  if (!slaveState.gyroReady) {
    messageOut = slaveState.lastErrorMessage.length() > 0
      ? slaveState.lastErrorMessage
      : "Gyro is not ready on the ESP8266 slave";
    return false;
  }

  return true;
}

bool gyroBeginIfEligible() {
  String ignored;
  return gyroValidateRuntime(ignored);
}

void gyroStop() {
  notifySlaveRuntimeChanged();
}

bool gyroIsRunning() {
  return isGyroToggleEnabled() && slaveState.present && slaveState.gyroReady && slaveState.mqttConnected;
}

void gyroLoop() {
  // The slave owns the gyro runtime. The master only consumes status.
}
