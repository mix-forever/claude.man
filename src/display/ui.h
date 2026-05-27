#pragma once
#include <Arduino.h>
#include "../network/api_client.h"

void uiShowConfigMode(const String& apSSID, const String& ip, const String& pass = "");
void uiShowConnecting(const String& ssid);
void uiShowMain(const RateLimit& rl, bool wifiOK, bool apiOK, int apiErrCode = -1, const String& localIP = "");
void uiShowError(const String& msg, int restartSecs = -1);
void uiShowTokenExpired(const String& localIP = "");
