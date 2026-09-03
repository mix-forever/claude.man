#pragma once
#include <Arduino.h>
#include "../network/api_client.h"

// reason: optional red headline (e.g. Wi-Fi failure) instead of "KONFIGURACJA"
void uiShowConfigMode(const String& apSSID, const String& ip, const String& pass = "",
                      const char* reason = nullptr);
void uiShowConnecting(const String& ssid);
void uiShowMain(const RateLimit& rl, bool wifiOK, bool apiOK, int apiErrCode = -1, const String& localIP = "");
void uiRefreshClock();
void uiRefreshCountdowns(const RateLimit& rl);
void uiShowError(const String& msg, int restartSecs = -1);
void uiShowTokenExpired(const String& localIP = "", const char* title = "TOKEN WYGASL (401)");
// Temporary feedback text in the bottom bar (x=90). Caller clears it via a full redraw.
void uiDrawOverlay(const char* text, uint16_t color);
// Full-screen one-line message (boot-time actions like factory reset).
void uiShowMessage(const char* msg);
// OTA progress screen: pct 0 draws the frame, 1..100 updates the bar, -1 = failure.
void uiShowOtaProgress(int pct);
