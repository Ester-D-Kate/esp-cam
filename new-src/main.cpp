#include "app_shared.h"

void setup() {
  // Disable routed application logging on master UART pins because they are
  // reserved for the ESP32-CAM <-> ESP8266 serial link.
  Serial.begin(115200, SERIAL_8N1, -1, -1);
  delay(250);

  preferences.begin("wifi-config", false);
  loadGyroSettings();
  loadConnectionError();
  initSlaveLink();

  if (!initCamera()) {
    Serial.println("Continuing without a working camera");
  }

  if (!initStorage()) {
    Serial.println("Continuing without a mounted SD card");
  }

  registerRoutes();
  if (loadCredentials(savedSsidCache, savedPasswordCache)) {
    PendingConnectionRequest request;
    request.active = true;
    request.keepAccessPoint = false;
    request.ssid = savedSsidCache;
    request.password = savedPasswordCache;
    request.gyroEnabled = isGyroToggleEnabled();
    request.validationRoute = getGyroValidationRoute();

    startConnectionAttempt(request);
  } else {
    startConfigMode();
  }
}

void loop() {
  server.handleClient();

  if (apActive) {
    dnsServer.processNextRequest();
  }

  processRuntimeState();
  delay(1);
}
