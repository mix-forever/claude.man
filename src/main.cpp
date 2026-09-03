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
static bool      hasWifiCfg      = false;
static RateLimit rateLimit;
static bool      apiOK           = false;
static int       lastHttp        = 0;
static uint32_t  errorStart      = 0;
static String    errorMsg        = "";
static bool      apiTaskStarted  = false;
static bool      mdnsStarted     = false;
static uint32_t  otaRedrawAt     = 0;   // after a failed OTA, restore the screen
static uint8_t   wifiFails       = 0;   // consecutive STA failures (from NVS)
static uint32_t  apRetryAt       = 0;   // AP fallback: when to retry stored Wi-Fi
static uint32_t  lastClockUpdate = 0;

// ─── Screen mode ─────────────────────────────────────────────────────────────
// While the full-screen token message is shown, the Pac-Man ticker, clock
// refresh and overlay restore must not draw the main layout over it.
static bool      tokenScreen     = false;

// ─── Overlay (temporary UI feedback) ─────────────────────────────────────────
static uint32_t  overlayClearAt  = 0;

// ─── Auto-dim ────────────────────────────────────────────────────────────────
static uint32_t  lastInteraction = 0;
static bool      autoDimmed      = false;

// ─── One-shot alarm ──────────────────────────────────────────────────────────
static bool      alarmArmed      = true;
static uint32_t  alarmStartedAt  = 0;

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

// Redraw whatever screen is current (main layout or token message).
static void redrawCurrentScreen() {
    if (tokenScreen) {
        uiShowTokenExpired(wifiLocalIP(),
                           apiTaskHasToken() ? "TOKEN WYGASL (401)" : "BRAK TOKENU");
    } else {
        uiShowMain(rateLimit, wifiIsConnected(), apiOK, lastHttp, wifiLocalIP());
    }
}

static void drawOverlay(const char* text, uint16_t color) {
    uiDrawOverlay(text, color);
    overlayClearAt = millis() + 1500;
}

// Called every loop: drives the one-shot alarm timeout as well as arming.
static void checkAlarm() {
    if (!rateLimit.valid || cfg.alarmThr == 0) {
        buzzerSetAlarmLevel(ALARM_NONE);
        alarmArmed = true;
        return;
    }
    float u   = rateLimit.util5h;
    float thr = cfg.alarmThr / 100.0f;

    if (u < thr) {
        buzzerSetAlarmLevel(ALARM_NONE);
        alarmArmed = true;   // re-arm: ready for next crossing
        return;
    }

    // Above threshold but alarm already fired this crossing — stop after timeout
    if (!alarmArmed) {
        if ((int32_t)(millis() - alarmStartedAt) >= ALARM_ONE_SHOT_MS)
            buzzerSetAlarmLevel(ALARM_NONE);
        return;
    }

    // First crossing: fire alarm once
    alarmArmed     = false;
    alarmStartedAt = millis();
    if      (u >= 1.0f) buzzerSetAlarmLevel(ALARM_CRITICAL);
    else if (u >= 0.9f) buzzerSetAlarmLevel(ALARM_HIGH);
    else                buzzerSetAlarmLevel(ALARM_LOW);
}

// ─── /status provider (called from inside webConfigHandle) ───────────────────

static void provideStatus(DeviceStatus& st) {
    st.valid      = rateLimit.valid;
    st.util5h     = rateLimit.util5h;
    st.util7d     = rateLimit.util7d;
    if (rateLimit.valid && ntpSynced()) {
        time_t now   = time(nullptr);
        st.reset5hIn = (int32_t)(rateLimit.reset5hAt - now);
        st.reset7dIn = (int32_t)(rateLimit.reset7dAt - now);
    }
    st.apiOK      = apiOK;
    st.lastHttp   = lastHttp;
    st.hasToken   = apiTaskHasToken();
    st.brightness = cfg.brightness;
    st.alarmThr   = cfg.alarmThr;
    st.mute       = cfg.buzzerMute;
}

// Enter AP setup mode. With stored Wi-Fi (fallback after failures) the form is
// prefilled with the SSID and the device retries the stored network later.
static void enterApMode(const char* reason) {
    String apSSID = buildApSSID();
    char apPass[9];
    generateApPassword(apPass, sizeof(apPass));
    if (!wifiStartAP(apSSID.c_str(), apPass)) {
        errorMsg = "AP init failed";
        uiShowError(errorMsg, 30);
        errorStart = millis();
        state = S_ERROR;
        return;
    }
    if (hasWifiCfg) {
        String notice = "Nie udało się połączyć z siecią <b>" + String(cfg.ssid)
                      + "</b> (" + String(WIFI_MAX_FAILS) + " próby). Sprawdź hasło. "
                        "Bez zmian urządzenie spróbuje ponownie za 10 minut.";
        webConfigBegin(true, cfg.ssid, notice.c_str(), cfg.apiKey[0] != '\0');
        apRetryAt = millis() + AP_RETRY_MS;
    } else {
        webConfigBegin(true);
    }
    uiShowConfigMode(apSSID, "192.168.4.1", apPass, reason);
    state = S_AP_MODE;
}

// ─── OTA progress (called from inside webConfigHandle) ───────────────────────

static void onOtaProgress(int pct) {
    if (pct == 0) buzzerSetAlarmLevel(ALARM_NONE);   // upload blocks the loop
    uiShowOtaProgress(pct);
    if (pct < 0) otaRedrawAt = millis() + 3000;
}

// ─── Button handlers ─────────────────────────────────────────────────────────
// Button handlers persist only their own fields (storageSaveSettings) so a
// token updated via the web dashboard is never overwritten by a stale copy.

static void handleBtnAlarmShort() {
    buzzerTestBeep();
}

static void handleBtnAlarmLong() {
    // Cycle: 80 → 90 → 0 → 80
    if (cfg.alarmThr == 80) cfg.alarmThr = 90;
    else if (cfg.alarmThr == 90) cfg.alarmThr = 0;
    else cfg.alarmThr = 80;
    storageSaveSettings(cfg);
    alarmArmed = true;          // new threshold → allow a fresh one-shot
    checkAlarm();
    pacmanSetThreshold(cfg.alarmThr / 100.0f);
    if (!tokenScreen) pacmanDraw();   // move Blinky to the new threshold

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
    storageSaveSettings(cfg);
    displaySetBrightness(v);

    char buf[12];
    snprintf(buf, sizeof(buf), "BL %d%%", v);
    drawOverlay(buf, 0xFFE0);
}

static void handleBtnBrightLong() {
    cfg.buzzerMute = !cfg.buzzerMute;
    buzzerMute(cfg.buzzerMute);
    storageSaveSettings(cfg);
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

    // Load config first so the boot melody and later alarms respect saved mute.
    hasWifiCfg = storageLoad(cfg);
    buzzerMute(cfg.buzzerMute);
    pacmanSetThreshold(cfg.alarmThr / 100.0f);
    buzzerPlayStart();

    // BOOT button (GPIO9): hold at power-on → factory reset (clear config → AP mode)
    pinMode(9, INPUT_PULLUP);
    delay(80);
    if (digitalRead(9) == LOW) {
        storageClear();
        uiShowMessage("Factory reset...");
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
            cfg.apiKey[0] = '\0';       // wipe token only
            storageSave(cfg);
            uiShowMessage("Token reset...");
            delay(1500);
            ESP.restart();
        }
    }
}

// ─── Loop ────────────────────────────────────────────────────────────────────

void loop() {
    switch (state) {

    // ── No Wi-Fi config, or too many failed connects: start AP ───────────
    case S_INIT:
        wifiFails = hasWifiCfg ? storageGetWifiFails() : 0;
        if (hasWifiCfg && wifiFails < WIFI_MAX_FAILS) {
            displaySetBrightness(cfg.brightness);
            state = S_CONNECTING;
        } else {
            if (hasWifiCfg) {
                storageSetWifiFails(0);   // next restart tries the stored network again
                displaySetBrightness(cfg.brightness);
            }
            enterApMode(hasWifiCfg ? "BLAD WIFI - SPRAWDZ HASLO" : nullptr);
        }
        break;

    // ── Serve config page until user submits and device restarts ─────────
    case S_AP_MODE:
        webConfigHandle();
        if (apRetryAt > 0 && (int32_t)(millis() - apRetryAt) >= 0) {
            ESP.restart();            // fail counter was cleared → S_CONNECTING
        }
        break;

    // ── Connect to WiFi, then start background API task ──────────────────
    // A missing token is fine here: the device connects, shows "BRAK TOKENU"
    // with its URL, and waits for the dashboard / sync-token.py to push one.
    case S_CONNECTING: {
        uiShowConnecting(cfg.ssid);

        bool connected = wifiConnectSTA(cfg.ssid, cfg.pass, WIFI_TIMEOUT_MS);

        if (connected) {
            if (wifiFails > 0) {
                wifiFails = 0;
                storageSetWifiFails(0);
            }
            ntpInit();
            if (!mdnsStarted && MDNS.begin("claude-monitor")) {
                MDNS.addService("http", "tcp", 80);   // announce → avahi caches us
                mdnsStarted = true;
            }
            webConfigSetOtaProgress(onOtaProgress);
            webConfigSetStatusProvider(provideStatus);
            webConfigBegin();
            uint32_t ntpWait = millis();
            while (!ntpSynced() && (int32_t)(millis() - ntpWait) < 3000) delay(100);

            if (!apiTaskStarted) {
                apiTaskStart(cfg.apiKey);
                apiTaskStarted = true;
            }

            // Keep the last known API state across a Wi-Fi reconnect.
            tokenScreen = false;
            uiShowMain(rateLimit, true, apiOK, lastHttp, wifiLocalIP());
            pacmanDraw();
            lastInteraction = millis();
            state = S_RUNNING;
        } else {
            // Count only boot-time failures; a mid-run Wi-Fi drop (wifiFails
            // already 0 after a successful connect) restarts without penalty.
            if (!apiTaskStarted) {
                wifiFails++;
                storageSetWifiFails(wifiFails);
            }
            errorMsg = "WiFi " + String(wifiFails) + "/" + String(WIFI_MAX_FAILS)
                     + " st:" + String(WiFi.status()) + " " + String(cfg.ssid);
            uiShowError(errorMsg, 30);
            errorStart = millis();
            state      = S_ERROR;
        }
        break;
    }

    // ── Normal operation: read async API results, keep UI alive ──────────
    case S_RUNNING: {
        buzzerAlarmTick();

        // Buttons — any press resets auto-dim. The press that wakes the
        // display is consumed and does not trigger its normal action.
        ButtonEvent evA = buttonsGetEvent(BTN_ALARM);
        ButtonEvent evB = buttonsGetEvent(BTN_BRIGHT);

        if (evA != EVT_NONE || evB != EVT_NONE) {
            lastInteraction = millis();
            if (autoDimmed) {
                autoDimmed = false;
                displaySetBrightness(cfg.brightness);
                evA = EVT_NONE;
                evB = EVT_NONE;
            }
        }

        if (evA == EVT_SHORT) handleBtnAlarmShort();
        else if (evA == EVT_LONG) handleBtnAlarmLong();

        if (evB == EVT_SHORT) handleBtnBrightShort();
        else if (evB == EVT_LONG) handleBtnBrightLong();

        // Auto-dim after inactivity
        if (!autoDimmed && (int32_t)(millis() - lastInteraction) >= AUTO_DIM_MS) {
            autoDimmed = true;
            displaySetBrightness(25);
        }

        // WiFi drop
        if (!wifiIsConnected()) {
            buzzerSetAlarmLevel(ALARM_NONE);   // reconnect blocks; don't leave a tone on
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
            overlayClearAt = 0;   // full redraw below supersedes any overlay
            if (!ok && code == 401) {
                tokenScreen = true;
                redrawCurrentScreen();
            } else {
                tokenScreen = false;
                if (rl.valid) {
                    pacmanSetFraction(rl.util5h);
                    pacmanDraw();
                }
                redrawCurrentScreen();
            }
        }

        checkAlarm();
        webConfigHandle();
        if (!tokenScreen) pacmanTick();

        // Clock + countdowns — refresh every minute (independent of API poll)
        if (!tokenScreen && overlayClearAt == 0 &&
            (int32_t)(millis() - lastClockUpdate) >= 60000) {
            lastClockUpdate = millis();
            uiRefreshClock();
            if (rateLimit.valid) uiRefreshCountdowns(rateLimit);
        }

        // Clear overlay after timeout
        if (overlayClearAt > 0 && (int32_t)(millis() - overlayClearAt) >= 0) {
            overlayClearAt = 0;
            redrawCurrentScreen();
        }

        // Restore the normal screen a few seconds after a failed OTA
        if (otaRedrawAt > 0 && (int32_t)(millis() - otaRedrawAt) >= 0) {
            otaRedrawAt = 0;
            redrawCurrentScreen();
            if (!tokenScreen) pacmanDraw();
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
