#pragma once
#include <Arduino.h>
#include "../storage/storage.h"

// OTA progress: 0..100 while flashing, -1 on failure. Called from inside
// webConfigHandle() (the upload blocks the main loop), so keep it cheap.
typedef void (*OtaProgressFn)(int pct);
void webConfigSetOtaProgress(OtaProgressFn fn);

// Live device state for GET /status. main.cpp fills this on request;
// web_config adds IP / RSSI / uptime / heap / firmware itself.
struct DeviceStatus {
    bool     valid      = false;   // rate-limit data present
    float    util5h     = 0.0f;
    float    util7d     = 0.0f;
    int32_t  reset5hIn  = -1;      // seconds until reset, -1 = unknown
    int32_t  reset7dIn  = -1;
    bool     apiOK      = false;
    int      lastHttp   = 0;
    bool     hasToken   = false;
    uint8_t  brightness = 0;
    uint8_t  alarmThr   = 0;
    bool     mute       = false;
};
typedef void (*StatusFn)(DeviceStatus& out);
void webConfigSetStatusProvider(StatusFn fn);

// apMode=true serves the setup form. prefillSsid / notice / hasToken make the
// form usable when the device fell back to AP after failed connections.
void webConfigBegin(bool apMode = false, const char* prefillSsid = nullptr,
                    const char* notice = nullptr, bool hasToken = false);
void webConfigHandle();
