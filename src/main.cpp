#include "app_shared.h"

void setup() {
  // Keep serial logs on UART0 TX (GPIO1) while leaving UART0 RX unassigned
  // so GPIO3 can be reused for the gyro during normal runtime.
  Serial.begin(115200, SERIAL_8N1, -1, 1);
  delay(250);
  Serial.println();
  Serial.println("ESP32-CAM booting");
  Serial.println("Serial logging is TX-only on GPIO1; keep programmer TX disconnected from GPIO3 for gyro control");

  if (indicatorPinUsable()) {
    pinMode(kIndicatorLedPin, OUTPUT);
    digitalWrite(kIndicatorLedPin, LOW);
    Serial.print("Indicator LED ready on GPIO");
    Serial.println(kIndicatorLedPin);
  } else {
    Serial.println("Indicator LED pin disabled because it conflicts with the active board wiring");
  }

  if (recordButtonPinUsable()) {
    pinMode(kRecordButtonPin, INPUT_PULLUP);
    Serial.print("Record button ready on GPIO");
    Serial.print(kRecordButtonPin);
    Serial.println(" (active LOW with INPUT_PULLUP)");
  } else {
    Serial.println("Record button pin disabled because it conflicts with the active board wiring");
  }

  if (!preferences.begin("wifi-config", false)) {
    Serial.println("Preferences unavailable; settings will not survive reboot");
  }
  WiFi.persistent(false);
  loadGyroSettings();
  loadConnectionError();

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
