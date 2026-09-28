#include "app_shared.h"
#include "esp_wifi.h"
#include <time.h>

const char kAccessPointSsid[] = "ESP32-CAM-Setup";
const char kAccessPointPassword[] = "12345678";
const char kMdnsHost[] = "esp32";
const uint16_t kDnsPort = 53;
const unsigned long kWifiConnectTimeoutMs = 15000;
const unsigned long kStatusHoldMs = 15000;
const unsigned long kErrorHoldMs = 3000;
const unsigned long kFastBlinkIntervalMs = 100;
const unsigned long kSlowBlinkIntervalMs = 500;
const unsigned long kRecordingFrameIntervalMs = 33;
const unsigned long kButtonDebounceMs = 50;
// User-requested 1-bit SD wiring plan:
// - LED on GPIO4
// - record button on GPIO12
const uint8_t kIndicatorLedPin = 4;
const uint8_t kRecordButtonPin = 12;

const IPAddress kApIp(192, 168, 4, 1);
const IPAddress kApSubnet(255, 255, 255, 0);

WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

bool cameraReady = false;
bool apActive = false;
bool mdnsActive = false;
bool httpServerStarted = false;
bool sdReady = false;
bool timeSyncStarted = false;
bool timeSynced = false;
bool streamClientActive = false;
bool streamStopRequested = false;

RuntimeMode runtimeMode = RuntimeMode::SoftApConfig;
PendingConnectionRequest pendingConnection;
RecordingSession recordingSession;
ScanState scanState;
SlaveState slaveState;
unsigned long runtimeModeStartedAt = 0;
bool startApWhenErrorWindowEnds = false;
String savedSsidCache;
String savedPasswordCache;

namespace {
constexpr char kSsidKey[] = "ssid";
constexpr char kPasswordKey[] = "pass";
constexpr char kLegacyPasswordKey[] = "password";
constexpr char kErrorTypeKey[] = "last_err_type";
constexpr char kErrorMessageKey[] = "last_err_msg";
constexpr time_t kValidTimeThreshold = 1700000000;
constexpr long kLocalUtcOffsetSeconds = 19800;  // Asia/Calcutta (UTC+05:30)

ErrorType sLastErrorType = ErrorType::None;
String sLastErrorMessage;
RuntimeMode sErrorReturnMode = RuntimeMode::SoftApConfig;

File sRecordingAviFile;
File sRecordingIndexFile;
String sRecordingAviPath;
String sRecordingIndexPath;
uint32_t sAviWidth = 0;
uint32_t sAviHeight = 0;
uint32_t sAviMaxFrameSize = 0;
uint32_t sAviRiffSizeOffset = 0;
uint32_t sAviMoviSizeOffset = 0;
uint32_t sAviAvihFramesOffset = 0;
uint32_t sAviAvihBytesPerSecondOffset = 0;
uint32_t sAviAvihSuggestedBufferOffset = 0;
uint32_t sAviStrhFramesOffset = 0;
uint32_t sAviStrhSuggestedBufferOffset = 0;
uint32_t sAviStrfImageSizeOffset = 0;
uint32_t sAviMoviDataStart = 0;

constexpr uint32_t kAviFramesPerSecond = 23;
constexpr uint32_t kAviMicrosecondsPerFrame = 1000000UL / kAviFramesPerSecond;

bool writeLe16(File& file, uint16_t value) {
  uint8_t bytes[2] = {
    static_cast<uint8_t>(value & 0xFF),
    static_cast<uint8_t>((value >> 8) & 0xFF)
  };
  return file.write(bytes, sizeof(bytes)) == sizeof(bytes);
}

bool writeLe32(File& file, uint32_t value) {
  uint8_t bytes[4] = {
    static_cast<uint8_t>(value & 0xFF),
    static_cast<uint8_t>((value >> 8) & 0xFF),
    static_cast<uint8_t>((value >> 16) & 0xFF),
    static_cast<uint8_t>((value >> 24) & 0xFF)
  };
  return file.write(bytes, sizeof(bytes)) == sizeof(bytes);
}

bool writeFourCc(File& file, const char* fourCc) {
  return file.write(reinterpret_cast<const uint8_t*>(fourCc), 4) == 4;
}

bool patchLe32(File& file, uint32_t offset, uint32_t value) {
  const size_t currentPosition = file.position();
  if (!file.seek(offset)) {
    return false;
  }

  const bool ok = writeLe32(file, value);
  return file.seek(currentPosition) && ok;
}

void closeRecordingFiles() {
  if (sRecordingAviFile) {
    sRecordingAviFile.close();
  }
  if (sRecordingIndexFile) {
    sRecordingIndexFile.close();
  }
}

void resetAviRecordingState() {
  closeRecordingFiles();
  sRecordingAviPath = "";
  sRecordingIndexPath = "";
  sAviWidth = 0;
  sAviHeight = 0;
  sAviMaxFrameSize = 0;
  sAviRiffSizeOffset = 0;
  sAviMoviSizeOffset = 0;
  sAviAvihFramesOffset = 0;
  sAviAvihBytesPerSecondOffset = 0;
  sAviAvihSuggestedBufferOffset = 0;
  sAviStrhFramesOffset = 0;
  sAviStrhSuggestedBufferOffset = 0;
  sAviStrfImageSizeOffset = 0;
  sAviMoviDataStart = 0;
}

bool writeAviHeader(uint32_t width, uint32_t height) {
  sAviWidth = width;
  sAviHeight = height;
  sAviRiffSizeOffset = 4;

  if (!writeFourCc(sRecordingAviFile, "RIFF") ||
      !writeLe32(sRecordingAviFile, 0) ||
      !writeFourCc(sRecordingAviFile, "AVI ") ||
      !writeFourCc(sRecordingAviFile, "LIST") ||
      !writeLe32(sRecordingAviFile, 192) ||
      !writeFourCc(sRecordingAviFile, "hdrl") ||
      !writeFourCc(sRecordingAviFile, "avih") ||
      !writeLe32(sRecordingAviFile, 56) ||
      !writeLe32(sRecordingAviFile, kAviMicrosecondsPerFrame)) {
    return false;
  }

  sAviAvihBytesPerSecondOffset = static_cast<uint32_t>(sRecordingAviFile.position());
  if (!writeLe32(sRecordingAviFile, 0) ||
      !writeLe32(sRecordingAviFile, 0) ||
      !writeLe32(sRecordingAviFile, 0x10)) {
    return false;
  }

  sAviAvihFramesOffset = static_cast<uint32_t>(sRecordingAviFile.position());
  sAviAvihSuggestedBufferOffset = sAviAvihFramesOffset + 12;

  if (!writeLe32(sRecordingAviFile, 0) ||
      !writeLe32(sRecordingAviFile, 0) ||
      !writeLe32(sRecordingAviFile, 1) ||
      !writeLe32(sRecordingAviFile, 0) ||
      !writeLe32(sRecordingAviFile, width) ||
      !writeLe32(sRecordingAviFile, height) ||
      !writeLe32(sRecordingAviFile, 0) ||
      !writeLe32(sRecordingAviFile, 0) ||
      !writeLe32(sRecordingAviFile, 0) ||
      !writeLe32(sRecordingAviFile, 0) ||
      !writeFourCc(sRecordingAviFile, "LIST") ||
      !writeLe32(sRecordingAviFile, 116) ||
      !writeFourCc(sRecordingAviFile, "strl") ||
      !writeFourCc(sRecordingAviFile, "strh") ||
      !writeLe32(sRecordingAviFile, 56) ||
      !writeFourCc(sRecordingAviFile, "vids") ||
      !writeFourCc(sRecordingAviFile, "MJPG") ||
      !writeLe32(sRecordingAviFile, 0) ||
      !writeLe16(sRecordingAviFile, 0) ||
      !writeLe16(sRecordingAviFile, 0) ||
      !writeLe32(sRecordingAviFile, 0) ||
      !writeLe32(sRecordingAviFile, 1) ||
      !writeLe32(sRecordingAviFile, kAviFramesPerSecond) ||
      !writeLe32(sRecordingAviFile, 0)) {
    return false;
  }

  sAviStrhFramesOffset = static_cast<uint32_t>(sRecordingAviFile.position());
  sAviStrhSuggestedBufferOffset = sAviStrhFramesOffset + 4;

  if (!writeLe32(sRecordingAviFile, 0) ||
      !writeLe32(sRecordingAviFile, 0) ||
      !writeLe32(sRecordingAviFile, 0xFFFFFFFF) ||
      !writeLe32(sRecordingAviFile, 0) ||
      !writeLe16(sRecordingAviFile, 0) ||
      !writeLe16(sRecordingAviFile, 0) ||
      !writeLe16(sRecordingAviFile, static_cast<uint16_t>(width)) ||
      !writeLe16(sRecordingAviFile, static_cast<uint16_t>(height)) ||
      !writeFourCc(sRecordingAviFile, "strf") ||
      !writeLe32(sRecordingAviFile, 40) ||
      !writeLe32(sRecordingAviFile, 40) ||
      !writeLe32(sRecordingAviFile, width) ||
      !writeLe32(sRecordingAviFile, height) ||
      !writeLe16(sRecordingAviFile, 1) ||
      !writeLe16(sRecordingAviFile, 24) ||
      !writeFourCc(sRecordingAviFile, "MJPG")) {
    return false;
  }

  sAviStrfImageSizeOffset = static_cast<uint32_t>(sRecordingAviFile.position());
  if (!writeLe32(sRecordingAviFile, 0) ||
      !writeLe32(sRecordingAviFile, 0) ||
      !writeLe32(sRecordingAviFile, 0) ||
      !writeLe32(sRecordingAviFile, 0) ||
      !writeLe32(sRecordingAviFile, 0) ||
      !writeFourCc(sRecordingAviFile, "LIST")) {
    return false;
  }

  sAviMoviSizeOffset = static_cast<uint32_t>(sRecordingAviFile.position());
  if (!writeLe32(sRecordingAviFile, 0) ||
      !writeFourCc(sRecordingAviFile, "movi")) {
    return false;
  }

  sAviMoviDataStart = static_cast<uint32_t>(sRecordingAviFile.position());
  return true;
}

bool beginAviRecordingFromFrame(const camera_fb_t* fb) {
  resetAviRecordingState();

  sRecordingAviPath = recordingSession.directory + "/recording.avi";
  sRecordingIndexPath = recordingSession.directory + "/recording.idx";

  sRecordingAviFile = SD_MMC.open(sRecordingAviPath.c_str(), FILE_WRITE);
  if (!sRecordingAviFile) {
    persistConnectionError(ErrorType::Recording, "Unable to open the AVI output file on the SD card");
    resetAviRecordingState();
    return false;
  }

  sRecordingIndexFile = SD_MMC.open(sRecordingIndexPath.c_str(), FILE_WRITE);
  if (!sRecordingIndexFile) {
    persistConnectionError(ErrorType::Recording, "Unable to open the AVI index file on the SD card");
    resetAviRecordingState();
    SD_MMC.remove(sRecordingAviPath.c_str());
    return false;
  }

  if (!writeAviHeader(static_cast<uint32_t>(fb->width), static_cast<uint32_t>(fb->height))) {
    persistConnectionError(ErrorType::Recording, "Unable to write the AVI file header");
    resetAviRecordingState();
    SD_MMC.remove(sRecordingAviPath.c_str());
    SD_MMC.remove(sRecordingIndexPath.c_str());
    return false;
  }

  return true;
}

bool appendAviFrame(const camera_fb_t* fb) {
  if (!sRecordingAviFile && !beginAviRecordingFromFrame(fb)) {
    return false;
  }

  const uint32_t frameLength = static_cast<uint32_t>(fb->len);
  const uint8_t paddingByte = 0;
  const uint32_t chunkOffset = static_cast<uint32_t>(sRecordingAviFile.position()) - sAviMoviDataStart + 4;

  if (!writeFourCc(sRecordingAviFile, "00dc") ||
      !writeLe32(sRecordingAviFile, frameLength) ||
      sRecordingAviFile.write(fb->buf, fb->len) != fb->len ||
      ((frameLength & 1U) != 0 && sRecordingAviFile.write(&paddingByte, 1) != 1)) {
    persistConnectionError(ErrorType::Recording, "Unable to append a video frame to the AVI file");
    return false;
  }

  if (!writeFourCc(sRecordingIndexFile, "00dc") ||
      !writeLe32(sRecordingIndexFile, 0x10) ||
      !writeLe32(sRecordingIndexFile, chunkOffset) ||
      !writeLe32(sRecordingIndexFile, frameLength)) {
    persistConnectionError(ErrorType::Recording, "Unable to append the AVI frame index");
    return false;
  }

  if (frameLength > sAviMaxFrameSize) {
    sAviMaxFrameSize = frameLength;
  }

  recordingSession.nextFrameNumber += 1;
  return true;
}

bool finalizeAviRecording() {
  if (!sRecordingAviFile) {
    resetAviRecordingState();
    return true;
  }

  const uint32_t frameCount = recordingSession.nextFrameNumber;
  const uint32_t moviEnd = static_cast<uint32_t>(sRecordingAviFile.position());
  const uint32_t moviSize = moviEnd - (sAviMoviSizeOffset + 4);

  if (!sRecordingIndexFile.seek(0)) {
    persistConnectionError(ErrorType::Recording, "Unable to rewind the AVI index file");
    resetAviRecordingState();
    return false;
  }

  const uint32_t indexSize = static_cast<uint32_t>(sRecordingIndexFile.size());
  if (!writeFourCc(sRecordingAviFile, "idx1") || !writeLe32(sRecordingAviFile, indexSize)) {
    persistConnectionError(ErrorType::Recording, "Unable to start the AVI index chunk");
    resetAviRecordingState();
    return false;
  }

  uint8_t buffer[512];
  while (true) {
    const size_t bytesRead = sRecordingIndexFile.read(buffer, sizeof(buffer));
    if (bytesRead == 0) {
      break;
    }
    if (sRecordingAviFile.write(buffer, bytesRead) != bytesRead) {
      persistConnectionError(ErrorType::Recording, "Unable to finish the AVI index chunk");
      resetAviRecordingState();
      return false;
    }
  }

  const uint32_t finalFileSize = static_cast<uint32_t>(sRecordingAviFile.position());
  const uint32_t bytesPerSecond = sAviMaxFrameSize * kAviFramesPerSecond;

  const bool patched =
    patchLe32(sRecordingAviFile, sAviRiffSizeOffset, finalFileSize - 8) &&
    patchLe32(sRecordingAviFile, sAviMoviSizeOffset, moviSize) &&
    patchLe32(sRecordingAviFile, sAviAvihFramesOffset, frameCount) &&
    patchLe32(sRecordingAviFile, sAviAvihBytesPerSecondOffset, bytesPerSecond) &&
    patchLe32(sRecordingAviFile, sAviAvihSuggestedBufferOffset, sAviMaxFrameSize) &&
    patchLe32(sRecordingAviFile, sAviStrhFramesOffset, frameCount) &&
    patchLe32(sRecordingAviFile, sAviStrhSuggestedBufferOffset, sAviMaxFrameSize) &&
    patchLe32(sRecordingAviFile, sAviStrfImageSizeOffset, sAviMaxFrameSize);

  if (!patched) {
    persistConnectionError(ErrorType::Recording, "Unable to finalize the AVI metadata");
    resetAviRecordingState();
    return false;
  }

  closeRecordingFiles();
  if (sRecordingIndexPath.length() > 0) {
    SD_MMC.remove(sRecordingIndexPath.c_str());
  }
  resetAviRecordingState();
  return true;
}

void removePreferenceKeyIfPresent(const char* key) {
  if (preferences.isKey(key)) {
    preferences.remove(key);
  }
}

bool pinConflictsWithSd1Bit(uint8_t pin) {
  return pin == 2 || pin == 14 || pin == 15;
}

bool pinConflictsWithCamera(uint8_t pin) {
  return pin == XCLK_GPIO_NUM ||
         pin == SIOD_GPIO_NUM ||
         pin == SIOC_GPIO_NUM ||
         pin == Y2_GPIO_NUM ||
         pin == Y3_GPIO_NUM ||
         pin == Y4_GPIO_NUM ||
         pin == Y5_GPIO_NUM ||
         pin == Y6_GPIO_NUM ||
         pin == Y7_GPIO_NUM ||
         pin == Y8_GPIO_NUM ||
         pin == Y9_GPIO_NUM ||
         pin == VSYNC_GPIO_NUM ||
         pin == HREF_GPIO_NUM ||
         pin == PCLK_GPIO_NUM ||
         pin == PWDN_GPIO_NUM;
}

void enterRuntimeMode(RuntimeMode mode) {
  runtimeMode = mode;
  runtimeModeStartedAt = millis();
}

ErrorType parseErrorType(const String& input) {
  String normalized = input;
  normalized.toLowerCase();

  if (normalized == "wifi") {
    return ErrorType::WiFi;
  }
  if (normalized == "gyro") {
    return ErrorType::Gyro;
  }
  if (normalized == "disconnect") {
    return ErrorType::Disconnect;
  }
  if (normalized == "sdcard") {
    return ErrorType::SDCard;
  }
  if (normalized == "recording") {
    return ErrorType::Recording;
  }
  return ErrorType::None;
}

String wifiFailureMessage() {
  switch (WiFi.status()) {
    case WL_NO_SSID_AVAIL:
      return "Configured Wi-Fi network is not available";
    case WL_CONNECT_FAILED:
      return "Wi-Fi authentication failed";
    case WL_CONNECTION_LOST:
      return "Wi-Fi connection was lost";
    case WL_DISCONNECTED:
      return "Wi-Fi did not connect in time";
    default:
      return "Unable to connect to Wi-Fi using the supplied credentials";
  }
}

void applyConnectedRadioTuning() {
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
}

void updateLedOutput() {
  if (!indicatorPinUsable()) {
    return;
  }

  bool ledOn = false;

  if (runtimeMode == RuntimeMode::Recording || runtimeMode == RuntimeMode::PostConnectValidation) {
    ledOn = true;
  } else if (runtimeMode == RuntimeMode::WifiConnecting) {
    ledOn = ((millis() / kSlowBlinkIntervalMs) % 2) == 0;
  } else if (runtimeMode == RuntimeMode::ErrorFallback) {
    ledOn = ((millis() / kFastBlinkIntervalMs) % 2) == 0;
  }

  digitalWrite(kIndicatorLedPin, ledOn ? HIGH : LOW);
}

void updateRecordButtonState() {
  recordingSession.pressedEdge = false;

  if (!recordButtonPinUsable()) {
    return;
  }

  bool physicalPressed = digitalRead(kRecordButtonPin) == LOW;

  if (physicalPressed != recordingSession.lastPhysicalPressed) {
    recordingSession.lastPhysicalPressed = physicalPressed;
    recordingSession.lastDebounceAt = millis();
  }

  if (millis() - recordingSession.lastDebounceAt < kButtonDebounceMs) {
    return;
  }

  if (recordingSession.stablePressed == physicalPressed) {
    return;
  }

  recordingSession.stablePressed = physicalPressed;
  recordingSession.pressedEdge = recordingSession.stablePressed;

  if (recordingSession.pressedEdge) {
    Serial.println("Record button click detected (debounced)");
    recordingSession.clickPending = true;
    recordingSession.clickHandled = false;
  }

  if (recordingSession.pressedEdge && streamClientActive) {
    requestStreamStop();
  }
}

bool recordingCanStartNow() {
  if (!recordButtonPinUsable() || !cameraReady || !sdReady || streamClientActive) {
    return false;
  }

  return runtimeMode == RuntimeMode::SoftApConfig || runtimeMode == RuntimeMode::ConnectedIdle;
}

String formatTimestampDirectory() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 10)) {
    return "";
  }

  char buffer[40];
  snprintf(
    buffer,
    sizeof(buffer),
    "/recordings/%04d-%02d-%02d_%02d-%02d-%02d",
    timeInfo.tm_year + 1900,
    timeInfo.tm_mon + 1,
    timeInfo.tm_mday,
    timeInfo.tm_hour,
    timeInfo.tm_min,
    timeInfo.tm_sec
  );

  String candidate = buffer;
  if (!SD_MMC.exists(candidate.c_str())) {
    return candidate;
  }

  for (uint16_t suffix = 1; suffix < 1000; ++suffix) {
    String suffixed = candidate + "_" + String(suffix);
    if (!SD_MMC.exists(suffixed.c_str())) {
      return suffixed;
    }
  }

  return "";
}

String buildNumericRecordingDirectory() {
  for (uint32_t index = 1; index < 1000000UL; ++index) {
    String candidate = "/recordings/" + String(index);
    if (!SD_MMC.exists(candidate.c_str())) {
      return candidate;
    }
  }

  return "";
}

bool ensureRecordingsRoot() {
  if (!SD_MMC.exists("/recordings")) {
    return SD_MMC.mkdir("/recordings");
  }
  return true;
}

bool prepareAndCommitConnection() {
  if (!pendingConnection.active) {
    return false;
  }

  bool candidateEnabled = false;
  String candidateRoute;
  String candidateUuid;
  String gyroMessage;
  bool uuidUpdated = false;

  if (!prepareGyroConfiguration(
        pendingConnection.gyroEnabled,
        pendingConnection.validationRoute,
        pendingConnection.email,
        pendingConnection.authPassword,
        candidateEnabled,
        candidateRoute,
        candidateUuid,
        gyroMessage,
        uuidUpdated
      )) {
    enterErrorFallback(ErrorType::Gyro, gyroMessage, pendingConnection.keepAccessPoint, RuntimeMode::SoftApConfig);
    return false;
  }

  bool previousEnabled = isGyroToggleEnabled();
  String previousRoute = getGyroValidationRoute();
  String previousUuid = getGyroIdentityUuid();

  setGyroRuntimeSettings(candidateEnabled, candidateRoute, candidateUuid);

  if (candidateEnabled) {
    if (!gyroValidateRuntime(gyroMessage)) {
      setGyroRuntimeSettings(previousEnabled, previousRoute, previousUuid);
      if (previousEnabled) {
        gyroBeginIfEligible();
      } else {
        gyroStop();
      }
      enterErrorFallback(ErrorType::Gyro, gyroMessage, pendingConnection.keepAccessPoint, RuntimeMode::SoftApConfig);
      return false;
    }
  } else {
    gyroStop();
  }

  saveCredentials(pendingConnection.ssid, pendingConnection.password);
  saveGyroSettings(candidateEnabled, candidateRoute, candidateUuid);

  clearConnectionError();
  pendingConnection = PendingConnectionRequest();
  beginSuccessWindow();
  return true;
}
}  // namespace

String escapeJson(const String& input) {
  String output;
  output.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); ++i) {
    char c = input[i];
    switch (c) {
      case '\\': output += "\\\\"; break;
      case '"': output += "\\\""; break;
      case '\n': output += "\\n"; break;
      case '\r': output += "\\r"; break;
      case '\t': output += "\\t"; break;
      default: output += c; break;
    }
  }
  return output;
}

String escapeHtml(const String& input) {
  String output;
  output.reserve(input.length() + 16);
  for (size_t i = 0; i < input.length(); ++i) {
    char c = input[i];
    switch (c) {
      case '&': output += "&amp;"; break;
      case '<': output += "&lt;"; break;
      case '>': output += "&gt;"; break;
      case '"': output += "&quot;"; break;
      case '\'': output += "&#39;"; break;
      default: output += c; break;
    }
  }
  return output;
}

String runtimeModeText() {
  switch (runtimeMode) {
    case RuntimeMode::SoftApConfig: return "SoftApConfig";
    case RuntimeMode::WifiConnecting: return "WifiConnecting";
    case RuntimeMode::PostConnectValidation: return "PostConnectValidation";
    case RuntimeMode::ConnectedIdle: return "ConnectedIdle";
    case RuntimeMode::Recording: return "Recording";
    case RuntimeMode::ErrorFallback: return "ErrorFallback";
  }
  return "Unknown";
}

String errorTypeText(ErrorType type) {
  switch (type) {
    case ErrorType::WiFi: return "wifi";
    case ErrorType::Gyro: return "gyro";
    case ErrorType::Disconnect: return "disconnect";
    case ErrorType::SDCard: return "sdcard";
    case ErrorType::Recording: return "recording";
    case ErrorType::None:
    default:
      return "none";
  }
}

ErrorType lastConnectionErrorType() {
  return sLastErrorType;
}

String lastConnectionErrorMessage() {
  return sLastErrorMessage;
}

bool startAccessPoint() {
  if (apActive) {
    return true;
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(kApIp, kApIp, kApSubnet);
  if (!WiFi.softAP(kAccessPointSsid, kAccessPointPassword)) {
    Serial.println("Access point start failed");
    return false;
  }

  dnsServer.start(kDnsPort, "*", kApIp);
  apActive = true;
  startHttpServerIfNeeded();

  Serial.print("AP SSID: ");
  Serial.println(kAccessPointSsid);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  return true;
}

void stopAccessPoint() {
  if (!apActive) {
    return;
  }

  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  apActive = false;

  if (WiFi.status() == WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
  }
}

void startConfigMode() {
  pendingConnection = PendingConnectionRequest();
  startApWhenErrorWindowEnds = false;
  stopMdnsService();

  if (!apActive) {
    startAccessPoint();
  }

  if (runtimeMode == RuntimeMode::Recording) {
    stopRecordingSession("Switching to configuration mode");
  }

  gyroStop();
  enterRuntimeMode(RuntimeMode::SoftApConfig);
}

void enterErrorFallback(ErrorType type, const String& message, bool startApNow, RuntimeMode returnMode) {
  if (runtimeMode == RuntimeMode::Recording) {
    stopRecordingSession("Error fallback");
  }

  Serial.print("Error fallback [");
  Serial.print(errorTypeText(type));
  Serial.print("]: ");
  Serial.println(message);

  persistConnectionError(type, message);
  pendingConnection = PendingConnectionRequest();
  gyroStop();
  streamStopRequested = true;
  sErrorReturnMode = returnMode;

  bool shouldReturnToSoftAp = (returnMode == RuntimeMode::SoftApConfig);
  if (shouldReturnToSoftAp) {
    stopMdnsService();
  }

  startApWhenErrorWindowEnds = shouldReturnToSoftAp && !startApNow;

  if (shouldReturnToSoftAp && startApNow) {
    startAccessPoint();
  }

  if (shouldReturnToSoftAp) {
    WiFi.disconnect(false, false);
  }

  enterRuntimeMode(RuntimeMode::ErrorFallback);
}

bool startConnectionAttempt(const PendingConnectionRequest& request) {
  if (request.ssid.length() == 0) {
    return false;
  }

  if (runtimeMode == RuntimeMode::Recording) {
    stopRecordingSession("Applying network settings");
  }

  pendingConnection = request;
  pendingConnection.active = true;
  stopMdnsService();
  gyroStop();
  streamStopRequested = true;

  WiFi.setHostname(kMdnsHost);
  WiFi.setAutoReconnect(true);

  if (pendingConnection.keepAccessPoint) {
    startAccessPoint();
    WiFi.mode(WIFI_AP_STA);
  } else {
    stopAccessPoint();
    WiFi.mode(WIFI_STA);
  }

  WiFi.disconnect(false, false);
  delay(50);

  if (pendingConnection.password.length() > 0) {
    WiFi.begin(pendingConnection.ssid.c_str(), pendingConnection.password.c_str());
  } else {
    WiFi.begin(pendingConnection.ssid.c_str());
  }

  Serial.print("Connecting to Wi-Fi SSID: ");
  Serial.println(pendingConnection.ssid);

  enterRuntimeMode(RuntimeMode::WifiConnecting);
  return true;
}

void beginSuccessWindow() {
  applyConnectedRadioTuning();
  startHttpServerIfNeeded();
  startMdnsServiceIfNeeded();
  syncClockIfNeeded();
  enterRuntimeMode(RuntimeMode::PostConnectValidation);
}

void transitionToConnectedIdle() {
  if (apActive) {
    stopAccessPoint();
  }

  startMdnsServiceIfNeeded();
  syncClockIfNeeded();
  gyroBeginIfEligible();
  enterRuntimeMode(RuntimeMode::ConnectedIdle);
}

RuntimeMode localFeatureErrorReturnMode() {
  return WiFi.status() == WL_CONNECTED ? RuntimeMode::ConnectedIdle : RuntimeMode::SoftApConfig;
}

void startHttpServerIfNeeded() {
  if (httpServerStarted) {
    return;
  }

  server.begin();
  httpServerStarted = true;
  Serial.println("HTTP server started");
}

bool loadCredentials(String& ssid, String& password) {
  ssid = preferences.getString(kSsidKey, "");
  password = preferences.getString(kPasswordKey, "");

  if (ssid.length() > 0) {
    savedSsidCache = ssid;
    savedPasswordCache = password;
    return true;
  }

  if (preferences.isKey(kLegacyPasswordKey)) {
    savedSsidCache = "";
    savedPasswordCache = preferences.getString(kLegacyPasswordKey, "");
    return false;
  }

  savedSsidCache = "";
  savedPasswordCache = "";
  return false;
}

void saveCredentials(const String& ssid, const String& password) {
  preferences.putString(kSsidKey, ssid);
  preferences.putString(kPasswordKey, password);
  removePreferenceKeyIfPresent(kLegacyPasswordKey);
  savedSsidCache = ssid;
  savedPasswordCache = password;
}

void clearCredentials() {
  removePreferenceKeyIfPresent(kSsidKey);
  removePreferenceKeyIfPresent(kPasswordKey);
  removePreferenceKeyIfPresent(kLegacyPasswordKey);
  savedSsidCache = "";
  savedPasswordCache = "";
}

void loadConnectionError() {
  sLastErrorType = preferences.isKey(kErrorTypeKey)
    ? parseErrorType(preferences.getString(kErrorTypeKey, "none"))
    : ErrorType::None;
  sLastErrorMessage = preferences.isKey(kErrorMessageKey)
    ? preferences.getString(kErrorMessageKey, "")
    : "";
}

void persistConnectionError(ErrorType type, const String& message) {
  sLastErrorType = type;
  sLastErrorMessage = message;
  preferences.putString(kErrorTypeKey, errorTypeText(type));
  preferences.putString(kErrorMessageKey, message);
}

void clearConnectionError() {
  sLastErrorType = ErrorType::None;
  sLastErrorMessage = "";
  removePreferenceKeyIfPresent(kErrorTypeKey);
  removePreferenceKeyIfPresent(kErrorMessageKey);
}

void stopMdnsService() {
  if (mdnsActive) {
    MDNS.end();
    mdnsActive = false;
  }
}

void startMdnsServiceIfNeeded() {
  if (mdnsActive || WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (MDNS.begin(kMdnsHost)) {
    MDNS.addService("http", "tcp", 80);
    mdnsActive = true;
    Serial.println("mDNS started at http://esp32.local/");
  } else {
    Serial.println("mDNS start failed");
  }
}

bool indicatorPinUsable() {
  return !pinConflictsWithCamera(kIndicatorLedPin) && !pinConflictsWithSd1Bit(kIndicatorLedPin);
}

bool recordButtonPinUsable() {
  return !pinConflictsWithCamera(kRecordButtonPin) && !pinConflictsWithSd1Bit(kRecordButtonPin);
}

void syncClockIfNeeded() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (!timeSyncStarted) {
    configTime(kLocalUtcOffsetSeconds, 0, "time.nist.gov", "0.pool.ntp.org", "1.pool.ntp.org");
    timeSyncStarted = true;
  }

  timeSynced = isTimeKnown();
}

bool initStorage() {
  sdReady = SD_MMC.begin("/sdcard", true);
  if (!sdReady) {
    Serial.println("SD_MMC mount failed");
    return false;
  }

  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("No SD card detected");
    sdReady = false;
    return false;
  }

  if (!ensureRecordingsRoot()) {
    Serial.println("Unable to create /recordings directory");
    sdReady = false;
    return false;
  }

  Serial.println("SD_MMC ready in 1-bit mode");
  return true;
}

bool isTimeKnown() {
  return time(nullptr) >= kValidTimeThreshold;
}

String buildNextRecordingDirectory() {
  if (!sdReady || !ensureRecordingsRoot()) {
    return "";
  }

  if (isTimeKnown()) {
    return formatTimestampDirectory();
  }

  return buildNumericRecordingDirectory();
}

bool startRecordingSession() {
  if (recordingSession.active || !recordingCanStartNow()) {
    return false;
  }

  String directory = buildNextRecordingDirectory();
  if (directory.length() == 0) {
    persistConnectionError(ErrorType::Recording, "Unable to allocate a recording directory");
    return false;
  }

  if (!SD_MMC.mkdir(directory.c_str())) {
    persistConnectionError(ErrorType::Recording, "Unable to create the recording directory");
    return false;
  }

  recordingSession.directory = directory;
  recordingSession.nextFrameNumber = 0;
  recordingSession.lastFrameAt = 0;
  recordingSession.modeBeforeRecording = runtimeMode;
  recordingSession.active = true;
  resetAviRecordingState();

  enterRuntimeMode(RuntimeMode::Recording);
  Serial.print("AVI recording started in ");
  Serial.println(recordingSession.directory);
  return true;
}

void stopRecordingSession(const String& reason) {
  if (!recordingSession.active) {
    return;
  }

  Serial.print("Recording stopped");
  if (reason.length() > 0) {
    Serial.print(": ");
    Serial.print(reason);
  }
  Serial.println();

  if (!finalizeAviRecording()) {
    Serial.println("AVI finalization failed");
  }

  recordingSession.active = false;
  recordingSession.lastFrameAt = 0;

  if (runtimeMode == RuntimeMode::Recording) {
    enterRuntimeMode(recordingSession.modeBeforeRecording);
  }
}

bool appendRecordingFrame() {
  if (!recordingSession.active || !cameraReady || !sdReady) {
    return false;
  }

  unsigned long now = millis();
  if (now - recordingSession.lastFrameAt < kRecordingFrameIntervalMs) {
    return true;
  }

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb || fb->format != PIXFORMAT_JPEG) {
    if (fb) {
      esp_camera_fb_return(fb);
    }
    persistConnectionError(ErrorType::Recording, "Camera capture failed during recording");
    return false;
  }

  const bool appended = appendAviFrame(fb);
  esp_camera_fb_return(fb);

  if (!appended) {
    return false;
  }

  recordingSession.lastFrameAt = now;
  return true;
}

String currentRecordingDirectory() {
  return recordingSession.directory;
}

bool scanNetworksAsync() {
  if (scanState.inProgress) {
    return true;
  }

  scanState.error = "";
  if (WiFi.getMode() == WIFI_OFF) {
    return false;
  }
  WiFi.scanDelete();

  int result = WiFi.scanNetworks(true, true);
  if (result == WIFI_SCAN_FAILED) {
    scanState.error = "Wi-Fi scan failed to start";
    scanState.jsonCache = "[]";
    scanState.inProgress = false;
    return false;
  }

  scanState.inProgress = true;
  return true;
}

void processWifiScan() {
  if (!scanState.inProgress) {
    return;
  }

  int result = WiFi.scanComplete();
  if (result == WIFI_SCAN_RUNNING) {
    return;
  }

  scanState.inProgress = false;
  scanState.lastUpdatedAt = millis();

  if (result == WIFI_SCAN_FAILED) {
    scanState.error = "Wi-Fi scan failed";
    scanState.jsonCache = "[]";
    WiFi.scanDelete();
    return;
  }

  String json = "[";
  for (int i = 0; i < result; ++i) {
    if (i > 0) {
      json += ",";
    }

    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) {
      ssid = "(hidden)";
    }

    json += "{\"ssid\":\"" + escapeJson(ssid) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
  }
  json += "]";

  scanState.error = "";
  scanState.jsonCache = json;
  WiFi.scanDelete();
}

bool streamAvailable() {
  return cameraReady && runtimeMode == RuntimeMode::ConnectedIdle && WiFi.status() == WL_CONNECTED;
}

void requestStreamStop() {
  streamStopRequested = true;
}

String buildStatusJson() {
  String json = "{";
  json += "\"mode\":\"" + escapeJson(runtimeModeText()) + "\"";
  json += ",\"apActive\":";
  json += apActive ? "true" : "false";
  json += ",\"wifiConnected\":";
  json += (WiFi.status() == WL_CONNECTED) ? "true" : "false";
  json += ",\"wifiStatusCode\":";
  json += String(static_cast<int>(WiFi.status()));
  json += ",\"streamAvailable\":";
  json += streamAvailable() ? "true" : "false";
  json += ",\"recordingActive\":";
  json += recordingSession.active ? "true" : "false";
  json += ",\"recordingDirectory\":\"" + escapeJson(currentRecordingDirectory()) + "\"";
  json += ",\"sdReady\":";
  json += sdReady ? "true" : "false";
  json += ",\"timeSynced\":";
  json += isTimeKnown() ? "true" : "false";
  json += ",\"gyroEnabled\":";
  json += isGyroToggleEnabled() ? "true" : "false";
  json += ",\"gyroAvailable\":";
  json += gyroHardwareAvailable() ? "true" : "false";
  json += ",\"gyroReady\":";
  json += gyroIsRunning() ? "true" : "false";
  json += ",\"lastErrorType\":\"" + escapeJson(errorTypeText(lastConnectionErrorType())) + "\"";
  json += ",\"lastErrorMessage\":\"" + escapeJson(lastConnectionErrorMessage()) + "\"";
  json += ",\"savedSsid\":\"" + escapeJson(savedSsidCache) + "\"";
  json += ",\"stationIp\":\"" + escapeJson(WiFi.localIP().toString()) + "\"";
  json += ",\"apIp\":\"" + escapeJson(kApIp.toString()) + "\"";
  json += ",\"scanInProgress\":";
  json += scanState.inProgress ? "true" : "false";
  json += ",\"scanError\":\"" + escapeJson(scanState.error) + "\"";
  json += ",\"networks\":";
  json += scanState.jsonCache;
  json += "}";
  return json;
}

void processRuntimeDuringStream() {
  processWifiScan();
  syncClockIfNeeded();
  updateRecordButtonState();

  if (recordingSession.stablePressed) {
    requestStreamStop();
  }

  if (runtimeMode == RuntimeMode::ConnectedIdle) {
    if (WiFi.status() != WL_CONNECTED) {
      requestStreamStop();
      enterErrorFallback(ErrorType::Disconnect, "Wi-Fi connection lost", true, RuntimeMode::SoftApConfig);
    } else {
      gyroLoop();
    }
  }

  updateLedOutput();
}

void processRuntimeState() {
  processWifiScan();
  syncClockIfNeeded();
  updateRecordButtonState();

  switch (runtimeMode) {
    case RuntimeMode::WifiConnecting:
      if (WiFi.status() == WL_CONNECTED) {
        applyConnectedRadioTuning();
        startMdnsServiceIfNeeded();
        syncClockIfNeeded();
        prepareAndCommitConnection();
      } else if (millis() - runtimeModeStartedAt >= kWifiConnectTimeoutMs) {
        enterErrorFallback(ErrorType::WiFi, wifiFailureMessage(), pendingConnection.keepAccessPoint, RuntimeMode::SoftApConfig);
      }
      break;

    case RuntimeMode::PostConnectValidation:
      if (WiFi.status() != WL_CONNECTED) {
        enterErrorFallback(ErrorType::Disconnect, "Wi-Fi connection lost during validation", true, RuntimeMode::SoftApConfig);
      } else {
        gyroLoop();
        if (millis() - runtimeModeStartedAt >= kStatusHoldMs) {
          transitionToConnectedIdle();
        }
      }
      break;

    case RuntimeMode::ConnectedIdle:
      if (WiFi.status() != WL_CONNECTED) {
        enterErrorFallback(ErrorType::Disconnect, "Wi-Fi connection lost", true, RuntimeMode::SoftApConfig);
      } else {
        gyroLoop();
      }
      break;

    case RuntimeMode::Recording:
      if (recordingSession.modeBeforeRecording == RuntimeMode::ConnectedIdle && WiFi.status() != WL_CONNECTED) {
        stopRecordingSession("Wi-Fi connection lost");
        enterErrorFallback(ErrorType::Disconnect, "Wi-Fi connection lost during recording", true, RuntimeMode::SoftApConfig);
      } else if (!appendRecordingFrame()) {
        stopRecordingSession("Frame write failed");
        enterErrorFallback(
          lastConnectionErrorType() == ErrorType::None ? ErrorType::Recording : lastConnectionErrorType(),
          lastConnectionErrorMessage().length() > 0 ? lastConnectionErrorMessage() : "Recording failed",
          false,
          localFeatureErrorReturnMode()
        );
      }
      break;

    case RuntimeMode::ErrorFallback:
      if (millis() - runtimeModeStartedAt >= kErrorHoldMs) {
        if (sErrorReturnMode == RuntimeMode::SoftApConfig) {
          if (startApWhenErrorWindowEnds && !apActive) {
            startAccessPoint();
          }
          enterRuntimeMode(RuntimeMode::SoftApConfig);
        } else if (sErrorReturnMode == RuntimeMode::ConnectedIdle) {
          transitionToConnectedIdle();
        } else {
          enterRuntimeMode(sErrorReturnMode);
        }
      }
      break;

    case RuntimeMode::SoftApConfig:
    default:
      break;
  }

  if (recordingSession.clickPending && !recordingSession.clickHandled) {
    RuntimeMode returnMode = localFeatureErrorReturnMode();

    if (recordingSession.active) {
      recordingSession.clickPending = false;
      recordingSession.clickHandled = true;
      stopRecordingSession("Button clicked");
    } else if (!cameraReady) {
      recordingSession.clickPending = false;
      recordingSession.clickHandled = true;
      enterErrorFallback(ErrorType::Recording, "Camera unavailable for recording", false, returnMode);
    } else if (!sdReady) {
      recordingSession.clickPending = false;
      recordingSession.clickHandled = true;
      enterErrorFallback(ErrorType::SDCard, "SD card is not available for recording", false, returnMode);
    } else if (streamClientActive) {
      requestStreamStop();
    } else if (recordingCanStartNow()) {
      recordingSession.clickPending = false;
      recordingSession.clickHandled = true;
      if (!startRecordingSession()) {
        enterErrorFallback(
          lastConnectionErrorType() == ErrorType::None ? ErrorType::Recording : lastConnectionErrorType(),
          lastConnectionErrorMessage().length() > 0 ? lastConnectionErrorMessage() : "Unable to start recording",
          false,
          returnMode
        );
      }
    }
  }

  updateLedOutput();
}
