#include "network.h"

#include <Arduino.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#include "app_config.h"
#include "display.h"

static int lastSubscriberCount = 0;

bool connectToWiFi(FetchError &outError) {
  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.begin(WIFI_SSID_VALUE, WIFI_PASSWORD_VALUE);

  const int maxAttempts = 80;
  for (int attempts = 0; WiFi.status() != WL_CONNECTED && attempts < maxAttempts; attempts++) {
    // Keep display active while waiting for WL_CONNECTED.
    showWifiConnectingFrame();
    yield();
  }

  if (WiFi.status() != WL_CONNECTED) {
    outError = ERROR_WIFI_CONNECT;
    logErrorWithHint(outError);
    return false;
  }

  outError = FETCH_OK;
  return true;
}

FetchError fetchSubscribers(int &outSubscribers) {
  if (SHOW_RANDOM_VALUE_FOR_TESTING) {
    // outSubscribers = random(-50, 5000) + 250;
    // outSubscribers = random(0, MAX_DISPLAY_VALUE + 1);
    outSubscribers = lastSubscriberCount + random(0, 14);  // For quick testing of animations
    lastSubscriberCount = outSubscribers;
    return FETCH_OK;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return ERROR_WIFI_NOT_CONNECTED;
  }

  const String apiUrl = String(API_URL_VALUE);

  HTTPClient http;
  bool beginOk = false;

  if (apiUrl.startsWith("https://")) {
    BearSSL::WiFiClientSecure secureClient;
    secureClient.setInsecure();
    beginOk = http.begin(secureClient, apiUrl);
  } else if (apiUrl.startsWith("http://")) {
    WiFiClient plainClient;
    beginOk = http.begin(plainClient, apiUrl);
  } else {
    return ERROR_HTTPS_INIT;
  }

  if (!beginOk) {
    return ERROR_HTTPS_INIT;
  }

  http.setTimeout(2500);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.GET();

  if (httpCode <= 0) {
    http.end();
    return ERROR_SERVER_NO_RESPONSE;
  }

  if (httpCode != HTTP_CODE_OK) {
    http.end();
    return ERROR_HTTP_STATUS;
  }

  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    return ERROR_JSON_PARSE;
  }

  if (!doc.containsKey("subscribers")) {
    return ERROR_SUBSCRIBERS_MISSING;
  }

  if (!doc["subscribers"].is<int>()) {
    return ERROR_SUBSCRIBERS_INVALID_TYPE;
  }

  outSubscribers = doc["subscribers"].as<int>();
  return FETCH_OK;
}

const char* errorDescription(FetchError errorCode) {
  switch (errorCode) {
    case FETCH_OK:
      return "no error";
    case ERROR_WIFI_CONNECT:
      return "WiFi connect failed. Check WIFI_SSID/WIFI_PASSWORD and router range.";
    case ERROR_WIFI_NOT_CONNECTED:
      return "WiFi is disconnected. Verify power stability and AP availability.";
    case ERROR_HTTPS_INIT:
      return "HTTPS init failed. Verify API_URL format (https://host/path).";
    case ERROR_SERVER_NO_RESPONSE:
      return "No response from server. Check backend status, DNS and internet access.";
    case ERROR_HTTP_STATUS:
      return "Server returned non-200 status. Check endpoint and auth/rate limits.";
    case ERROR_JSON_PARSE:
      return "Response is not valid JSON. Inspect backend payload.";
    case ERROR_SUBSCRIBERS_MISSING:
      return "JSON field 'subscribers' is missing. Backend must return it.";
    case ERROR_SUBSCRIBERS_INVALID_TYPE:
      return "Field 'subscribers' has invalid type. Must be integer.";
    default:
      return "unknown error";
  }
}

void logErrorWithHint(FetchError errorCode) {
  if (!ENABLE_SERIAL_LOG) {
    return;
  }

  Serial.print("[ERROR] E");
  if (static_cast<int>(errorCode) < 10) {
    Serial.print("0");
  }
  Serial.print(static_cast<int>(errorCode));
  Serial.print(": ");
  Serial.println(errorDescription(errorCode));
}
