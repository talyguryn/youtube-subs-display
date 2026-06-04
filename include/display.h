#pragma once

#include <Arduino.h>
#include "app_types.h"

void initDisplay();
void clearDigits();

void setDisplayBlank();
void setDisplayNumberFrame(int number);
void displayNumberWithoutLeadingZeros(int number);

void waitFrameCycles(int cycles);
void displayErrorCodeFrame(FetchError errorCode);
void animateRunningDash();
void showWifiConnectingFrame();
void animateNumberDrumTransition(int fromNumber, int toNumber);
