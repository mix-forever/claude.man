#include "storage.h"
#include "../../include/config.h"
#include <Preferences.h>
#include <string.h>

static const char* NS = "claude-mon";

bool storageLoad(AppConfig& cfg) {
    Preferences p;
    p.begin(NS, true);
    String s = p.getString("ssid",   "");
    strncpy(cfg.ssid, s.c_str(), sizeof(cfg.ssid) - 1);
    cfg.ssid[sizeof(cfg.ssid) - 1] = '\0';
    s = p.getString("pass",   "");
    strncpy(cfg.pass, s.c_str(), sizeof(cfg.pass) - 1);
    cfg.pass[sizeof(cfg.pass) - 1] = '\0';
    s = p.getString("apikey", "");
    strncpy(cfg.apiKey, s.c_str(), sizeof(cfg.apiKey) - 1);
    cfg.apiKey[sizeof(cfg.apiKey) - 1] = '\0';

    cfg.brightness = p.getUChar("brightness", DEFAULT_BRIGHTNESS);
    cfg.alarmThr   = p.getUChar("alarm_thr",  DEFAULT_ALARM_THR);
    cfg.buzzerMute = p.getBool("buzzer_mute", false);
    p.end();
    return cfg.ssid[0] != '\0';
}

void storageSave(const AppConfig& cfg) {
    Preferences p;
    p.begin(NS, false);
    p.putString("ssid",   cfg.ssid);
    p.putString("pass",   cfg.pass);
    p.putString("apikey", cfg.apiKey);
    p.putUChar("brightness", cfg.brightness);
    p.putUChar("alarm_thr",  cfg.alarmThr);
    p.putBool("buzzer_mute", cfg.buzzerMute);
    p.end();
}

void storageSaveSettings(const AppConfig& cfg) {
    Preferences p;
    p.begin(NS, false);
    p.putUChar("brightness", cfg.brightness);
    p.putUChar("alarm_thr",  cfg.alarmThr);
    p.putBool("buzzer_mute", cfg.buzzerMute);
    p.end();
}

void storageClear() {
    Preferences p;
    p.begin(NS, false);
    p.clear();
    p.end();
}

uint8_t storageGetWifiFails() {
    Preferences p;
    p.begin(NS, true);
    uint8_t n = p.getUChar("wifi_fail", 0);
    p.end();
    return n;
}

void storageSetWifiFails(uint8_t n) {
    Preferences p;
    p.begin(NS, false);
    if (p.getUChar("wifi_fail", 0) != n) p.putUChar("wifi_fail", n);   // spare NVS wear
    p.end();
}
