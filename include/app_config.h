#pragma once

#include <Arduino.h>
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

extern const char* WIFI_SSID_VALUE;
extern const char* WIFI_PASSWORD_VALUE;
extern const char* API_URL_VALUE;

static const bool SHOW_RANDOM_VALUE_FOR_TESTING = false;
static const bool ENABLE_SERIAL_LOG = false;

const unsigned long UPDATE_INTERVAL = 5UL * 60UL * 1000UL;  // 5 minutes
// static const unsigned long UPDATE_INTERVAL = 2UL * 1000UL;

static const int MAX_DISPLAY_VALUE = 9999;
static const int DISPLAY_REFRESH_FRAMES = 50;
static const unsigned int DISPLAY_TICK_US = 2000;
static const int FETCH_ANIMATION_FRAME_CYCLES = 18;
