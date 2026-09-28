#include "app_shared.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace {
constexpr char kGyroEnabledKey[] = "gyro_enabled";
constexpr char kGyroRouteKey[] = "gyro_route";
constexpr char kGyroUuidKey[] = "gyro_uuid";

bool sGyroEnabled = false;
String sGyroRoute;
String sGyroUuid;

String trimTrailingSlashes(const String& input) {
  String output = input;
  output.trim();
  while (output.endsWith("/")) {
    output.remove(output.length() - 1);
  }
  return output;
}

String buildValidationUrl(const String& input) {
  String normalized = trimTrailingSlashes(input);

  if (normalized.endsWith("/api/gaze/validate/uuid")) {
    return normalized;
  }

  if (normalized.endsWith("/api")) {
    return normalized + "/gaze/validate/uuid";
  }

  return normalized + "/api/gaze/validate/uuid";
}

bool isValidValidationUrl(const String& input) {
  return input.startsWith("http://") || input.startsWith("https://");
}

bool extractJsonStringField(const String& json, const char* key, String& valueOut) {
  valueOut = "";

  String needle = String("\"") + key + "\"";
  int keyIndex = json.indexOf(needle);
  if (keyIndex < 0) {
    return false;
  }

  int colonIndex = json.indexOf(':', keyIndex + needle.length());
  if (colonIndex < 0) {
    return false;
  }

  int firstQuoteIndex = json.indexOf('"', colonIndex + 1);
  if (firstQuoteIndex < 0) {
    return false;
  }

  String value;
  bool escaped = false;
  for (size_t i = static_cast<size_t>(firstQuoteIndex + 1); i < json.length(); ++i) {
    char c = json[i];

    if (escaped) {
      switch (c) {
        case 'n': value += '\n'; break;
        case 'r': value += '\r'; break;
        case 't': value += '\t'; break;
        case '"': value += '"'; break;
        case '\\': value += '\\'; break;
        default: value += c; break;
      }
      escaped = false;
      continue;
    }

    if (c == '\\') {
      escaped = true;
      continue;
    }

    if (c == '"') {
      valueOut = value;
      return true;
    }

    value += c;
  }

  return false;
}

bool requestGyroIdentity(
  const String& validationUrl,
  const String& email,
  const String& password,
  String& uuidOut,
  String& errorOut
) {
  uuidOut = "";
  errorOut = "";

  if (WiFi.status() != WL_CONNECTED) {
    errorOut = "Wi-Fi is not connected";
    return false;
  }

  if (!isValidValidationUrl(validationUrl)) {
    errorOut = "Validation route must start with http:// or https://";
    return false;
  }

  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  WiFiClient* transportClient = &plainClient;

  if (validationUrl.startsWith("https://")) {
    secureClient.setInsecure();
    transportClient = &secureClient;
  }

  HTTPClient http;
  if (!http.begin(*transportClient, validationUrl)) {
    errorOut = "Unable to start validation request";
    http.end();
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  String requestBody = "{\"email\":\"" + escapeJson(email) + "\",\"password\":\"" + escapeJson(password) + "\"}";

  int code = http.POST(requestBody);
  String responseBody = http.getString();
  http.end();

  if (code != 200 && code != 201) {
    errorOut = "Validation failed (HTTP " + String(code) + ")";
    return false;
  }

  const char* keys[] = {"hashed_uuid", "uuid", "identity", "deviceId", "id"};
  for (size_t i = 0; i < (sizeof(keys) / sizeof(keys[0])); ++i) {
    String candidate;
    if (extractJsonStringField(responseBody, keys[i], candidate) && candidate.length() > 0) {
      uuidOut = candidate;
      return true;
    }
  }

  errorOut = "Validation succeeded but UUID was not found in response";
  return false;
}
}  // namespace

void loadGyroSettings() {
  sGyroEnabled = preferences.getBool(kGyroEnabledKey, false);
  sGyroRoute = preferences.isKey(kGyroRouteKey)
    ? trimTrailingSlashes(preferences.getString(kGyroRouteKey, ""))
    : "";
  sGyroUuid = preferences.isKey(kGyroUuidKey)
    ? preferences.getString(kGyroUuidKey, "")
    : "";

  if (sGyroRoute.length() > 0) {
    sGyroRoute = buildValidationUrl(sGyroRoute);
  }

  if (sGyroEnabled && sGyroUuid.length() == 0) {
    sGyroEnabled = false;
    preferences.putBool(kGyroEnabledKey, false);
    Serial.println("Gyro was enabled without a UUID. It has been disabled.");
  }
}

bool isGyroToggleEnabled() {
  return sGyroEnabled;
}

String getGyroValidationRoute() {
  return sGyroRoute;
}

String getGyroIdentityUuid() {
  return sGyroUuid;
}

void setGyroRuntimeSettings(bool enabled, const String& validationRoute, const String& identityUuid) {
  sGyroEnabled = enabled;
  sGyroRoute = trimTrailingSlashes(validationRoute);
  if (sGyroRoute.length() > 0) {
    sGyroRoute = buildValidationUrl(sGyroRoute);
  }
  sGyroUuid = identityUuid;
}

void saveGyroSettings(bool enabled, const String& validationRoute, const String& identityUuid) {
  setGyroRuntimeSettings(enabled, validationRoute, identityUuid);
  preferences.putBool(kGyroEnabledKey, sGyroEnabled);
  preferences.putString(kGyroRouteKey, sGyroRoute);
  preferences.putString(kGyroUuidKey, sGyroUuid);
}

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
) {
  enabledOut = false;
  routeOut = trimTrailingSlashes(validationRoute.length() > 0 ? validationRoute : sGyroRoute);
  uuidOut = sGyroUuid;
  messageOut = "";
  uuidUpdated = false;

  if (routeOut.length() > 0) {
    routeOut = buildValidationUrl(routeOut);
  }

  if (!requestedEnable) {
    messageOut = "Gyro/authentication disabled";
    return true;
  }

  if (!gyroHardwareAvailable()) {
    messageOut = "Gyro hardware is not available on GPIO3/GPIO13";
    return false;
  }

  if (routeOut.length() == 0) {
    messageOut = "Validation route is required when gyro is enabled";
    return false;
  }

  bool hasEmail = email.length() > 0;
  bool hasPassword = password.length() > 0;
  if (hasEmail != hasPassword) {
    messageOut = "Enter both email and password, or leave both empty";
    return false;
  }

  if (hasEmail && hasPassword) {
    String requestError;
    String newUuid;
    if (!requestGyroIdentity(routeOut, email, password, newUuid, requestError)) {
      messageOut = requestError;
      return false;
    }

    uuidUpdated = (newUuid != sGyroUuid);
    uuidOut = newUuid;
  } else if (uuidOut.length() == 0) {
    messageOut = "Validate once with email/password to create a UUID";
    return false;
  }

  enabledOut = true;
  messageOut = uuidUpdated ? "Gyro identity updated" : "Gyro identity ready";
  return true;
}
