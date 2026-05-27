#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <time.h>

#include "config.h"
#include "storage/storage.h"
#include "network/wifi_mgr.h"
#include "network/api_client.h"
#include "network/api_task.h"
#include "network/web_config.h"
#include "display/display.h"
#include "display/ui.h"
#include "display/pacman.h"
#include "hardware/buttons.h"
#include "hardware/buzzer.h"

enum State { S_INIT, S_AP_MODE, S_CONNECTING, S_RUNNING, S_ERROR };

static State     state           = S_INIT;
static AppConfig cfg;
static RateLimit rateLimit;
static bool      apiOK           = false;
static int       lastHttp        = 0;
static uint32_t  errorStart      = 0;
static String    errorMsg        = "";
static bool      apiTaskStarted  = false;

// ─── Overlay (temporary UI feedback) ─────────────────────────────────────────
static uint32_t  overlayClearAt  = 0;
static uint16_t  overlayColor    = TFT_WHITE;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static String buildApSSID() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[5];
    snprintf(buf, sizeof(buf), "%02X%02X", mac[4], mac[5]);
    return String(AP_SSID_PREFIX) + "-" + buf;
}

static void generateApPassword(char* out, size_t len) {
    const char charset[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    size_t n = len - 1;
    for (size_t i = 0; i < n; i++) {
        out[i] = charset[esp_random() % (sizeof(charset) - 1)];
    }
    out[n] = '\0';
}

static void ntpInit() {
    configTzTime(TIMEZONE_STRING, NTP_SERVER);
}

static bool ntpSynced() {
    return time(nullptr) > 1000000000UL;
}

static void drawOverlay(const char* text, uint16_t color) {
    int16_t w = strlen(text) * 6;
    int16_t x = 90;            // between 7D % and WiFi dot
    int16_t y = 61;
    tft.fillRect(x - 2, y - 2, w + 4, 10, TFT_BLACK);
    tft.setTextColor(color, TFT_BLACK);
    tft.setCursor(x, y);
    tft.print(text);
    overlayClearAt = millis() + 1500;
    overlayColor   = color;
}

static void checkAlarm() {
    if (!rateLimit.valid || cfg.alarmThr == 0) {
        buzzerSetAlarmLevel(ALARM_NONE);
        return;
    }
    float u = rateLimit.util5h;
    if (u < cfg.alarmThr / 100.0f) {
        buzzerSetAlarmLevel(ALARM_NONE);
    } else if (u >= 1.0f) {
        buzzerSetAlarmLevel(ALARM_CRITICAL);
    } else if (u >= 0.9f) {
        buzzerSetAlarmLevel(ALARM_HIGH);
    } else {
        buzzerSetAlarmLevel(ALARM_LOW);
    }
}

// ─── Button handlers ─────────────────────────────────────────────────────────

static void handleBtnAlarmShort() {
    buzzerTestBeep();
}

static void handleBtnAlarmLong() {
    // Cycle: 80 → 90 → 0 → 80
    if (cfg.alarmThr == 80) cfg.alarmThr = 90;
    else if (cfg.alarmThr == 90) cfg.alarmThr = 0;
    else cfg.alarmThr = 80;
    storageSave(cfg);
    checkAlarm();

    char buf[12];
    if (cfg.alarmThr == 0) snprintf(buf, sizeof(buf), "AL OFF");
    else snprintf(buf, sizeof(buf), "AL %d%%", cfg.alarmThr);
    drawOverlay(buf, 0xF800);
}

static void handleBtnBrightShort() {
    uint8_t v = cfg.brightness;
    if (v <= 25)      v = 50;
    else if (v <= 50) v = 75;
    else if (v <= 75) v = 100;
    else              v = 25;
    cfg.brightness = v;
    storageSave(cfg);
    displaySetBrightness(v);

    char buf[12];
    snprintf(buf, sizeof(buf), "BL %d%%", v);
    drawOverlay(buf, 0xFFE0);
}

static void handleBtnBrightLong() {
    cfg.buzzerMute = !cfg.buzzerMute;
    buzzerMute(cfg.buzzerMute);
    storageSave(cfg);
    if (cfg.buzzerMute) {
        buzzerSetAlarmLevel(ALARM_NONE);
        drawOverlay("MUTE ON", 0xF800);
    } else {
        drawOverlay("MUTE OFF", 0xFFE0);
        checkAlarm();
    }
}

// ─── Setup ───────────────────────────────────────────────────────────────────

void setup() {
    displayInit();
    buttonsInit();
    buzzerInit();
    buzzerPlayStart();

    // BOOT button (GPIO9): hold at power-on → factory reset (clear config → AP mode)
    pinMode(9, INPUT_PULLUP);
    delay(80);
    if (digitalRead(9) == LOW) {
        storageClear();
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(4, 30);
        tft.print("Factory reset...");
        delay(1500);
        ESP.restart();
    }

    // BTN_ALARM (GPIO2): hold at power-on ≥3 s → reset only token (keep WiFi)
    if (buttonsIsPressed(BTN_ALARM)) {
        uint32_t t0 = millis();
        while (buttonsIsPressed(BTN_ALARM) && (int32_t)(millis() - t0) < 3000) {
            delay(50);
        }
        if (buttonsIsPressed(BTN_ALARM)) {
            AppConfig tmp;
            storageLoad(tmp);           // load whatever exists
            tmp.apiKey[0] = '\0';       // wipe token only
            storageSave(tmp);
            tft.fillScreen(TFT_BLACK);
            tft.setTextColor(TFT_YELLOW, TFT_BLACK);
            tft.setTextSize(1);
            tft.setCursor(4, 30);
            tft.print("Token reset...");
            delay(1500);
            ESP.restart();
        }
    }
}

// ─── Loop ────────────────────────────────────────────────────────────────────

void loop() {
    switch (state) {

    // ── First boot or no config: start AP ────────────────────────────────
    case S_INIT:
        if (storageLoad(cfg)) {
            displaySetBrightness(cfg.brightness);
            state = S_CONNECTING;
        } else {
            String apSSID = buildApSSID();
            char apPass[9];
            generateApPassword(apPass, sizeof(apPass));
            if (!wifiStartAP(apSSID.c_str(), apPass)) {
                errorMsg = "AP init failed";
                uiShowError(errorMsg, 30);
                errorStart = millis();
                state = S_ERROR;
                break;
            }
            webConfigBegin(true);
            uiShowConfigMode(apSSID, "192.168.4.1", apPass);
            state = S_AP_MODE;
        }
        break;

    // ── Serve config page until user submits and device restarts ─────────
    case S_AP_MODE:
        webConfigHandle();
        break;

    // ── Connect to WiFi, then start background API task ──────────────────
    case S_CONNECTING: {
        uiShowConnecting(cfg.ssid);

        bool connected = wifiConnectSTA(cfg.ssid, cfg.pass, WIFI_TIMEOUT_MS);

        if (connected) {
            ntpInit();
            MDNS.begin("claude-monitor");
            webConfigBegin();
            uint32_t ntpWait = millis();
            while (!ntpSynced() && (int32_t)(millis() - ntpWait) < 3000) delay(100);

            if (!apiTaskStarted) {
                apiTaskStart(cfg.apiKey);
                apiTaskStarted = true;
            }

            uiShowMain(rateLimit, true, false, 0, wifiLocalIP());
            pacmanDraw();
            state = S_RUNNING;
        } else {
            errorMsg = "st:" + String(WiFi.status())
                     + " ssid:" + String(cfg.ssid);
            uiShowError(errorMsg, 30);
            errorStart = millis();
            state      = S_ERROR;
        }
        break;
    }

    // ── Normal operation: read async API results, keep UI alive ──────────
    case S_RUNNING: {
        buzzerAlarmTick();

        // Buttons
        ButtonEvent evA = buttonsGetEvent(BTN_ALARM);
        if (evA == EVT_SHORT) handleBtnAlarmShort();
        else if (evA == EVT_LONG) handleBtnAlarmLong();

        ButtonEvent evB = buttonsGetEvent(BTN_BRIGHT);
        if (evB == EVT_SHORT) handleBtnBrightShort();
        else if (evB == EVT_LONG) handleBtnBrightLong();

        // WiFi drop
        if (!wifiIsConnected()) {
            uiShowError("WiFi rozlaczone", -1);
            delay(2000);
            state = S_CONNECTING;
            break;
        }

        // API result
        if (apiTaskHasResult()) {
            RateLimit rl;
            bool ok;
            int code;
            apiTaskGetResult(rl, ok, code);
            rateLimit = rl;
            apiOK     = ok;
            lastHttp  = code;
            if (!ok && code == 401) {
                uiShowTokenExpired(wifiLocalIP());
            } else {
                if (rl.valid) {
                    pacmanSetFraction(rl.util5h);
                    pacmanDraw();
                }
                uiShowMain(rateLimit, wifiIsConnected(), apiOK, lastHttp, wifiLocalIP());
            }
            checkAlarm();
        }

        webConfigHandle();
        pacmanTick();

        // Clear overlay after timeout
        if (overlayClearAt > 0 && (int32_t)(millis() - overlayClearAt) >= 0) {
            overlayClearAt = 0;
            uiShowMain(rateLimit, wifiIsConnected(), apiOK, lastHttp, wifiLocalIP());
        }
        break;
    }

    // ── WiFi error: show countdown, restart (config preserved) ──────────
    case S_ERROR: {
        int secsLeft = 30 - (int)((millis() - errorStart) / 1000);
        if (secsLeft <= 0) {
            ESP.restart();
        }
        uiShowError(errorMsg, secsLeft);
        delay(1000);
        break;
    }

    } // switch
}
