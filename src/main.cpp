#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "env_config.h"

#ifndef WIFI_SSID
#error "WIFI_SSID is not defined. Put WIFI_SSID in .env"
#endif

#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD is not defined. Put WIFI_PASSWORD in .env"
#endif

#ifndef API_URL
#error "API_URL is not defined. Put API_URL in .env"
#endif

// Connection constants
const char* WIFI_SSID_VALUE = WIFI_SSID;
const char* WIFI_PASSWORD_VALUE = WIFI_PASSWORD;

const char* API_URL_VALUE = API_URL;
const unsigned long UPDATE_INTERVAL = 5UL * 60UL * 1000UL;  // 5 minutes
const int MAX_DISPLAY_VALUE = 9999;
const int DISPLAY_REFRESH_FRAMES = 100;

// Display pins (Wemos ESP-12F GPIO)
const int PIN_SEGMENT_A = D2;   // GPIO4
const int PIN_SEGMENT_B = D1;   // GPIO5
const int PIN_SEGMENT_C = D6;   // GPIO12
const int PIN_SEGMENT_D = D7;   // GPIO13
const int PIN_SEGMENT_E = D5;   // GPIO14
const int PIN_SEGMENT_F = D8;   // GPIO15
const int PIN_SEGMENT_G = D0;   // GPIO16

// Digit select pins
const int PIN_DIGIT_1 = D4;     // GPIO2  - thousands
const int PIN_DIGIT_2 = 3;      // GPIO3  - hundreds (RX)
const int PIN_DIGIT_3 = 1;      // GPIO1  - tens (TX)
const int PIN_DIGIT_4 = D3;     // GPIO0  - ones

// 7-segment table (bits: dp g f e d c b a)
const byte SEGMENT_CODES[10] = {
  0b00111111,  // 0: a b c d e f
  0b00000110,  // 1: b c
  0b01011011,  // 2: a b d e g
  0b01001111,  // 3: a b c d g
  0b01100110,  // 4: b c f g
  0b01101101,  // 5: a c d f g
  0b01111101,  // 6: a c d e f g
  0b00000111,  // 7: a b c
  0b01111111,  // 8: all segments
  0b01101111   // 9: a b c d f g
};

// Global state
unsigned long lastUpdateTime = 0;
int subscriberCount = 0;
bool wifiConnected = false;
bool firstDataFetch = true;

// Forward declarations
void clearDigits();
void displaySegments(byte code);
void showDigit(int digit, int position);
void initDisplay();
int clampDisplayNumber(int number);
void splitDigits(int number, int outDigits[4]);
int findMostSignificantDigitPos(const int digits[4]);

// Display helpers

int clampDisplayNumber(int number) {
  if (number < 0) return 0;
  if (number > MAX_DISPLAY_VALUE) return MAX_DISPLAY_VALUE;
  return number;
}

void splitDigits(int number, int outDigits[4]) {
  outDigits[0] = number % 10;           // ones
  outDigits[1] = (number / 10) % 10;    // tens
  outDigits[2] = (number / 100) % 10;   // hundreds
  outDigits[3] = (number / 1000) % 10;  // thousands
}

int findMostSignificantDigitPos(const int digits[4]) {
  int startPos = 3;
  while (startPos > 0 && digits[startPos] == 0) {
    startPos--;
  }
  return startPos;
}

// Animation: dash moving left-right (while connecting to WiFi)
void animateRunningDash() {
  const int duration = 1500;  // milliseconds
  unsigned long startTime = millis();

  while (millis() - startTime < duration) {
    for (int pos = 0; pos < 4; pos++) {
      clearDigits();
      displaySegments(0b01000000);  // only segment G (dash)
      switch (pos) {
        case 0: digitalWrite(PIN_DIGIT_4, LOW); break;  // ones
        case 1: digitalWrite(PIN_DIGIT_3, LOW); break;  // tens
        case 2: digitalWrite(PIN_DIGIT_2, LOW); break;  // hundreds
        case 3: digitalWrite(PIN_DIGIT_1, LOW); break;  // thousands
      }
      delay(150);
    }
    for (int pos = 3; pos >= 1; pos--) {
      clearDigits();
      displaySegments(0b01000000);  // only segment G (dash)
      switch (pos) {
        case 1: digitalWrite(PIN_DIGIT_3, LOW); break;
        case 2: digitalWrite(PIN_DIGIT_2, LOW); break;
        case 3: digitalWrite(PIN_DIGIT_1, LOW); break;
      }
      delay(150);
    }
  }
  clearDigits();
}

// Animation: stroke running around the outline on all 4 digits
void animateRunningStroke() {
  // Stroke segment order: a -> b -> c -> d -> e -> f
  byte strokeSequence[6] = {
    0b00000001,  // a (top)
    0b00000010,  // b (upper-right)
    0b00000100,  // c (lower-right)
    0b00001000,  // d (bottom)
    0b00010000,  // e (lower-left)
    0b00100000   // f (upper-left)
  };

  for (int cycle = 0; cycle < 2; cycle++) {  // 2 full rounds
    for (int seg = 0; seg < 6; seg++) {
      clearDigits();
      displaySegments(strokeSequence[seg]);
      // Activate all 4 digits at the same time
      digitalWrite(PIN_DIGIT_1, LOW);
      digitalWrite(PIN_DIGIT_2, LOW);
      digitalWrite(PIN_DIGIT_3, LOW);
      digitalWrite(PIN_DIGIT_4, LOW);
      delay(120);
    }
  }
  clearDigits();
}

void initDisplay() {
  pinMode(PIN_SEGMENT_A, OUTPUT);
  pinMode(PIN_SEGMENT_B, OUTPUT);
  pinMode(PIN_SEGMENT_C, OUTPUT);
  pinMode(PIN_SEGMENT_D, OUTPUT);
  pinMode(PIN_SEGMENT_E, OUTPUT);
  pinMode(PIN_SEGMENT_F, OUTPUT);
  pinMode(PIN_SEGMENT_G, OUTPUT);

  pinMode(PIN_DIGIT_1, OUTPUT);
  pinMode(PIN_DIGIT_2, OUTPUT);
  pinMode(PIN_DIGIT_3, OUTPUT);
  pinMode(PIN_DIGIT_4, OUTPUT);

  // Turn off all segments
  digitalWrite(PIN_SEGMENT_A, LOW);
  digitalWrite(PIN_SEGMENT_B, LOW);
  digitalWrite(PIN_SEGMENT_C, LOW);
  digitalWrite(PIN_SEGMENT_D, LOW);
  digitalWrite(PIN_SEGMENT_E, LOW);
  digitalWrite(PIN_SEGMENT_F, LOW);
  digitalWrite(PIN_SEGMENT_G, LOW);

  // Disable all digits
  digitalWrite(PIN_DIGIT_1, HIGH);
  digitalWrite(PIN_DIGIT_2, HIGH);
  digitalWrite(PIN_DIGIT_3, HIGH);
  digitalWrite(PIN_DIGIT_4, HIGH);
}

// Set segments for one digit
void displaySegments(byte code) {
  digitalWrite(PIN_SEGMENT_A, (code & 0x01) ? HIGH : LOW);
  digitalWrite(PIN_SEGMENT_B, (code & 0x02) ? HIGH : LOW);
  digitalWrite(PIN_SEGMENT_C, (code & 0x04) ? HIGH : LOW);
  digitalWrite(PIN_SEGMENT_D, (code & 0x08) ? HIGH : LOW);
  digitalWrite(PIN_SEGMENT_E, (code & 0x10) ? HIGH : LOW);
  digitalWrite(PIN_SEGMENT_F, (code & 0x20) ? HIGH : LOW);
  digitalWrite(PIN_SEGMENT_G, (code & 0x40) ? HIGH : LOW);
}

// Disable all digits and clear segments
void clearDigits() {
  digitalWrite(PIN_DIGIT_1, HIGH);  // common cathode: HIGH = off
  digitalWrite(PIN_DIGIT_2, HIGH);
  digitalWrite(PIN_DIGIT_3, HIGH);
  digitalWrite(PIN_DIGIT_4, HIGH);

  digitalWrite(PIN_SEGMENT_A, LOW);
  digitalWrite(PIN_SEGMENT_B, LOW);
  digitalWrite(PIN_SEGMENT_C, LOW);
  digitalWrite(PIN_SEGMENT_D, LOW);
  digitalWrite(PIN_SEGMENT_E, LOW);
  digitalWrite(PIN_SEGMENT_F, LOW);
  digitalWrite(PIN_SEGMENT_G, LOW);
}

// Show one digit
void showDigit(int digit, int position) {
  // 0 - ones, 1 - tens, 2 - hundreds, 3 - thousands
  clearDigits();

  if (digit < 0 || digit > 9) digit = 0;

  displaySegments(SEGMENT_CODES[digit]);

  // Enable selected digit (common cathode: LOW = on)
  switch (position) {
    case 0: digitalWrite(PIN_DIGIT_4, LOW); break;  // ones
    case 1: digitalWrite(PIN_DIGIT_3, LOW); break;  // tens
    case 2: digitalWrite(PIN_DIGIT_2, LOW); break;  // hundreds
    case 3: digitalWrite(PIN_DIGIT_1, LOW); break;  // thousands
  }
}

// Show a 4-digit number without leading zeros (one multiplex frame)
void displayNumberWithoutLeadingZeros(int number) {
  number = clampDisplayNumber(number);

  int digits[4];
  splitDigits(number, digits);

  int startPos = findMostSignificantDigitPos(digits);

  // One fast multiplex cycle for smooth rendering
  for (int pos = startPos; pos >= 0; pos--) {
    showDigit(digits[pos], pos);
    delayMicroseconds(500);
  }
}

// WiFi

void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID_VALUE, WIFI_PASSWORD_VALUE);

  const int maxAttempts = 40;

  for (int attempts = 0; WiFi.status() != WL_CONNECTED && attempts < maxAttempts; attempts++) {
    animateRunningDash();
  }

  clearDigits();
  delay(100);

  wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (!wifiConnected) {
    Serial.println("[WARN] WiFi connection failed");
  }
}

// API

bool fetchSubscribers(int &outSubscribers) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ERROR] WiFi is not connected");
    return false;
  }

  BearSSL::WiFiClientSecure client;
  client.setInsecure();  // HTTPS without certificate validation

  HTTPClient https;
  if (!https.begin(client, API_URL_VALUE)) {
    Serial.println("[ERROR] HTTPS initialization failed");
    return false;
  }

  https.addHeader("Content-Type", "application/json");
  int httpCode = https.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.print("[ERROR] HTTP status: ");
    Serial.println(httpCode);
    https.end();
    return false;
  }

  String payload = https.getString();
  https.end();

  // Parse JSON response
  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, payload);

  if (err) {
    Serial.print("[ERROR] JSON parse error: ");
    Serial.println(err.c_str());
    return false;
  }

  // Validate subscribers field
  if (!doc.containsKey("subscribers")) {
    Serial.println("[ERROR] 'subscribers' field is missing");
    return false;
  }

  if (!doc["subscribers"].is<int>()) {
    Serial.println("[ERROR] 'subscribers' has invalid type");
    return false;
  }

  outSubscribers = doc["subscribers"].as<int>();
  return true;
}

void fetchSubscriberCount() {
  int newCount = 0;
  if (fetchSubscribers(newCount)) {
    subscriberCount = newCount;
    // Play the startup animation once on first successful fetch
    if (firstDataFetch) {
      animateRunningStroke();
      firstDataFetch = false;
    }
  } else {
    Serial.println("[WARN] Failed to fetch data, keeping previous value");
  }
}

// Setup and main loop

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("[INFO] YouTube Subscribers Display started");

  initDisplay();
  clearDigits();

  connectToWiFi();

  delay(1000);
  fetchSubscriberCount();
  lastUpdateTime = millis();
}

void loop() {
  // Reconnect WiFi if needed
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiConnected) {
      Serial.println("[WARN] WiFi disconnected, reconnecting...");
      wifiConnected = false;
    }
    connectToWiFi();
  }

  // Update subscriber count periodically
  unsigned long currentTime = millis();
  if (currentTime - lastUpdateTime >= UPDATE_INTERVAL) {
    fetchSubscriberCount();
    lastUpdateTime = currentTime;
  }

  // Keep refreshing the display to avoid flicker
  for (int frame = 0; frame < DISPLAY_REFRESH_FRAMES; frame++) {
    displayNumberWithoutLeadingZeros(subscriberCount);
  }
}