#pragma once
#include <Arduino.h>

bool   wifiStartAP(const char* ssid, const char* pass);
bool   wifiConnectSTA(const char* ssid, const char* pass, uint32_t timeoutMs);
bool   wifiIsConnected();
String wifiLocalIP();
