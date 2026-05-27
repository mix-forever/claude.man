#pragma once
#include <Arduino.h>

struct AppConfig {
    char    ssid[33];
    char    pass[65];
    char    apiKey[513];
    uint8_t brightness;   // 0-100
    uint8_t alarmThr;     // 0=off, 70, 80, 90
    bool    buzzerMute;
};

bool  storageLoad(AppConfig& cfg);
void  storageSave(const AppConfig& cfg);
void  storageClear();
