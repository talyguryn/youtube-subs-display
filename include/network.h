#pragma once

#include "app_types.h"

bool connectToWiFi(FetchError &outError);
FetchError fetchSubscribers(int &outSubscribers);
const char* errorDescription(FetchError errorCode);
void logErrorWithHint(FetchError errorCode);
