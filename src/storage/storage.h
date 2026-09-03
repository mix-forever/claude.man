#pragma once
#include <Arduino.h>

struct AppConfig {
    char    ssid[33];
    char    pass[65];
    char    apiKey[513];
    uint8_t brightness;   // 0-100
    uint8_t alarmThr;     // 0=off, 80, 90
    bool    buzzerMute;
};

// Returns true when Wi-Fi credentials exist (token may still be empty).
bool  storageLoad(AppConfig& cfg);
void  storageSave(const AppConfig& cfg);
// Writes only brightness / alarmThr / buzzerMute — never touches Wi-Fi or token.
void  storageSaveSettings(const AppConfig& cfg);
void  storageClear();
// Consecutive failed STA connections (NVS key wifi_fail). Reset on success.
uint8_t storageGetWifiFails();
void    storageSetWifiFails(uint8_t n);
