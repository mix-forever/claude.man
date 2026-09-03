#pragma once

// ─── Display ────────────────────────────────────────────────────────────────
#define DISPLAY_WIDTH    284
#define DISPLAY_HEIGHT   76
#define DISPLAY_ROTATION 1

// ─── SPI Pins (ESP32-C3 SuperMini) ──────────────────────────────────────────
#define PIN_TFT_MOSI  6
#define PIN_TFT_SCLK  4
#define PIN_TFT_CS    5
#define PIN_TFT_DC    7
#define PIN_TFT_RST   8
#define PIN_TFT_BL    10   // PWM CH 0 (backlight dimming)

// ─── Buttons (INPUT_PULLUP) ─────────────────────────────────────────────────
#define PIN_BTN_ALARM   2   // Button A: test / long = alarm threshold
#define PIN_BTN_BRIGHT  3   // Button B: brightness / long = mute toggle (shares GPIO3 with TFT_MISO dummy — both input-only, no conflict)

// ─── Buzzer (passive, NPN via GPIO 0) ───────────────────────────────────────
#define PIN_BUZZER      0
#define PWM_CH_BL       0
#define PWM_CH_BUZZER   1
#define PWM_FREQ_BL     5000
#define PWM_FREQ_BUZZ   2000
#define PWM_RES_BITS    8

// ─── Defaults ─────────────────────────────────────────────────────────────────
#define DEFAULT_BRIGHTNESS  100
#define DEFAULT_ALARM_THR   80

// ─── Timings ─────────────────────────────────────────────────────────────────
#define AUTO_DIM_MS         300000   // 5 min without button press → dim to 25%
#define ALARM_ONE_SHOT_MS   10000    // play alarm for 10 s, then silence until next crossing

// ─── App ────────────────────────────────────────────────────────────────────
#define FW_VERSION       "1.1.0"      // shown in dashboard footer and /status
#define AP_SSID_PREFIX   "Claude-Monitor"
#define WIFI_TIMEOUT_MS  30000
#define WIFI_MAX_FAILS   3            // consecutive STA failures → fall back to AP setup
#define AP_RETRY_MS      600000       // AP fallback: retry stored Wi-Fi after 10 min
#define API_POLL_MS      180000
#define API_HOST         "api.anthropic.com"
#define API_MODEL        "claude-haiku-4-5-20251001"
#define NTP_SERVER       "pool.ntp.org"
#define TIMEZONE_STRING  "CET-1CEST,M3.5.0,M10.5.0/3"
