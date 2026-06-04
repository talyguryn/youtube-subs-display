#include <Arduino.h>
#include <ESP8266WiFi.h>

#include "app_config.h"
#include "app_types.h"
#include "display.h"
#include "network.h"

static unsigned long gLastUpdateTime = 0;
static int gSubscriberCount = 0;
static bool gWifiConnected = false;
static bool gInitialFetchPending = true;
static bool gJustAnimated = false;
static FetchError gCurrentError = FETCH_OK;

static void fetchSubscriberCount(bool animateFromZero) {
  int previousCount = gSubscriberCount;
  int newCount = 0;
  FetchError fetchError = fetchSubscribers(newCount);

  if (fetchError == FETCH_OK) {
    gCurrentError = FETCH_OK;

    if (newCount != gSubscriberCount) {
      int animationStart = animateFromZero ? 0 : previousCount;
      setDisplayNumberFrame(animationStart);
      waitFrameCycles(2);
      animateNumberDrumTransition(animationStart, newCount);
      gSubscriberCount = newCount;
      gJustAnimated = true;  // Flag to skip redundant display
      if (ENABLE_SERIAL_LOG) {
        Serial.print("[INFO] Subscribers updated: ");
        Serial.println(gSubscriberCount);
      }
    } else {
      setDisplayNumberFrame(gSubscriberCount);
      gJustAnimated = false;
      if (ENABLE_SERIAL_LOG) {
        Serial.println("[INFO] Subscribers unchanged, display update skipped");
      }
    }
    return;
  }

  gCurrentError = fetchError;
  gJustAnimated = false;
  logErrorWithHint(fetchError);
  if (ENABLE_SERIAL_LOG) {
    Serial.println("[WARN] Failed to fetch data, keeping previous value");
  }
}

void setup() {
  if (ENABLE_SERIAL_LOG) {
    Serial.begin(115200);
    delay(100);
    Serial.println("[INFO] YouTube Subscribers Display started");
  }

  initDisplay();
  clearDigits();

  gSubscriberCount = 0;
  gInitialFetchPending = true;
  gLastUpdateTime = millis();

  if (connectToWiFi(gCurrentError)) {
    gWifiConnected = true;
  } else {
    gWifiConnected = false;
  }
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    if (gWifiConnected) {
      if (ENABLE_SERIAL_LOG) {
        Serial.println("[WARN] WiFi disconnected, reconnecting...");
      }
      gWifiConnected = false;
    }

    if (!connectToWiFi(gCurrentError)) {
      displayErrorCodeFrame(gCurrentError);
      return;
    }

    gWifiConnected = true;
    gInitialFetchPending = true;
    gJustAnimated = false;
  }

  unsigned long currentTime = millis();
  if (gInitialFetchPending || currentTime - gLastUpdateTime >= UPDATE_INTERVAL) {
    fetchSubscriberCount(gInitialFetchPending);

    gLastUpdateTime = currentTime;
    gInitialFetchPending = false;
  }

  if (gCurrentError != FETCH_OK) {
    displayErrorCodeFrame(gCurrentError);
    return;
  }

  // Skip redundant display if we just finished animation
  if (!gJustAnimated) {
    displayNumberWithoutLeadingZeros(gSubscriberCount);
  } else {
    gJustAnimated = false;  // Reset for next cycle
    delay(1);
  }
}
