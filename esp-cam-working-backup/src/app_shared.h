#pragma once

#include <Arduino.h>
#include "esp_camera.h"
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <FS.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <WebServer.h>
#include <WiFi.h>

#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

enum class RuntimeMode : uint8_t {
  SoftApConfig,
  WifiConnecting,
  PostConnectValidation,
  ConnectedIdle,
  Recording,
  ErrorFallback
};

enum class ErrorType : uint8_t {
  None,
  WiFi,
  Gyro,
  Disconnect,
  SDCard,
  Recording
};

struct PendingConnectionRequest {
  bool active = false;
  bool keepAccessPoint = false;
  String ssid;
  String password;
  bool gyroEnabled = false;
  String validationRoute;
  String email;
  String authPassword;
};

struct RecordingSession {
  bool active = false;
  bool stablePressed = false;
  bool lastPhysicalPressed = false;
  bool clickPending = false;
  bool clickHandled = false;
  bool pressedEdge = false;
  unsigned long lastDebounceAt = 0;
  String directory;
  uint32_t nextFrameNumber = 1;
  unsigned long lastFrameAt = 0;
  RuntimeMode modeBeforeRecording = RuntimeMode::SoftApConfig;
};

struct ScanState {
  bool inProgress = false;
  String jsonCache = "[]";
  String error;
  unsigned long lastUpdatedAt = 0;
};

struct SlaveState {
  bool present = false;
  bool helloReceived = false;
  bool buttonClickPending = false;
  bool configReady = false;
  bool runtimeReady = false;
  bool gyroPresent = false;
  bool gyroReady = false;
  bool wifiConnected = false;
  bool mqttConnected = false;
  bool configDirty = true;
  bool runtimeDirty = true;
  unsigned long syncNotBeforeAt = 0;
  unsigned long lastSeenAt = 0;
  unsigned long lastConfigSentAt = 0;
  unsigned long lastRuntimeSentAt = 0;
  String firmwareVersion;
  String lastErrorType;
  String lastErrorMessage;
};

extern const char kAccessPointSsid[];
extern const char kAccessPointPassword[];
extern const char kMdnsHost[];
extern const uint16_t kDnsPort;
extern const unsigned long kWifiConnectTimeoutMs;
extern const unsigned long kStatusHoldMs;
extern const unsigned long kErrorHoldMs;
extern const unsigned long kFastBlinkIntervalMs;
extern const unsigned long kSlowBlinkIntervalMs;
extern const unsigned long kRecordingFrameIntervalMs;
extern const unsigned long kButtonDebounceMs;
extern const uint8_t kIndicatorLedPin;
extern const uint8_t kRecordButtonPin;

extern const IPAddress kApIp;
extern const IPAddress kApSubnet;

extern WebServer server;
extern DNSServer dnsServer;
extern Preferences preferences;

extern bool cameraReady;
extern bool apActive;
extern bool mdnsActive;
extern bool httpServerStarted;
extern bool sdReady;
extern bool timeSyncStarted;
extern bool timeSynced;
extern bool streamClientActive;
extern bool streamStopRequested;

extern RuntimeMode runtimeMode;
extern PendingConnectionRequest pendingConnection;
extern RecordingSession recordingSession;
extern ScanState scanState;
extern SlaveState slaveState;
extern unsigned long runtimeModeStartedAt;
extern bool startApWhenErrorWindowEnds;
extern String savedSsidCache;
extern String savedPasswordCache;

String escapeJson(const String& input);
String escapeHtml(const String& input);
String runtimeModeText();
String errorTypeText(ErrorType type);
ErrorType lastConnectionErrorType();
String lastConnectionErrorMessage();

bool initCamera();
void handleStream();
bool indicatorPinUsable();
bool recordButtonPinUsable();

bool startAccessPoint();
void stopAccessPoint();
void startConfigMode();
void enterErrorFallback(ErrorType type, const String& message, bool startApNow, RuntimeMode returnMode = RuntimeMode::SoftApConfig);
bool startConnectionAttempt(const PendingConnectionRequest& request);
void beginSuccessWindow();
void transitionToConnectedIdle();
void processRuntimeState();
void processRuntimeDuringStream();
void startHttpServerIfNeeded();

bool streamAvailable();
void requestStreamStop();
String buildStatusJson();

bool initStorage();
bool isTimeKnown();
String buildNextRecordingDirectory();
bool startRecordingSession();
void stopRecordingSession(const String& reason = "");
bool appendRecordingFrame();
String currentRecordingDirectory();

bool scanNetworksAsync();
void processWifiScan();

bool loadCredentials(String& ssid, String& password);
void saveCredentials(const String& ssid, const String& password);
void clearCredentials();

void loadConnectionError();
void persistConnectionError(ErrorType type, const String& message);
void clearConnectionError();

void startMdnsServiceIfNeeded();
void stopMdnsService();
void syncClockIfNeeded();
void initSlaveLink();
void processSlaveLink();
void notifySlaveConfigChanged();
void notifySlaveRuntimeChanged();
String currentSlaveLedMode();

void registerRoutes();

void loadGyroSettings();
bool isGyroToggleEnabled();
String getGyroValidationRoute();
String getGyroIdentityUuid();
void setGyroRuntimeSettings(bool enabled, const String& validationRoute, const String& identityUuid);
void saveGyroSettings(bool enabled, const String& validationRoute, const String& identityUuid);
bool prepareGyroConfiguration(
  bool requestedEnable,
  const String& validationRoute,
  const String& email,
  const String& password,
  bool& enabledOut,
  String& routeOut,
  String& uuidOut,
  String& messageOut,
  bool& uuidUpdated
);

bool gyroHardwareAvailable();
void gyroStop();
bool gyroBeginIfEligible();
bool gyroValidateRuntime(String& messageOut);
bool gyroIsRunning();
void gyroLoop();
