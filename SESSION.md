# SESSION.md — Claude-Man Technical Reference

> Single source of truth for the current firmware state.  
> Last updated: 2026-05-26

---

## 1. Project Overview

Claude-Man is an ESP32-C3 SuperMini-based physical dashboard that monitors Claude Code session usage limits via the Anthropic OAuth usage API. It renders a Pac-Man themed UI on a 76×284 ST7789 display and provides audible alarms, hardware brightness control, and a captive web dashboard with OTA updates.

**Key design decisions:**
- Non-blocking architecture: API polling runs in a dedicated FreeRTOS task
- ~1 AI token per poll: uses `POST /v1/messages` with `max_tokens: 1` and reads rate-limit headers
- Fixed-size config struct to avoid heap fragmentation
- TLS verified via embedded root CA (GTS Root R4)
- All runtime settings persisted in NVS (brightness, alarm threshold, mute)

---

## 2. Hardware Specification

| Component | Model / Type | Interface | Notes |
|---|---|---|---|
| MCU | ESP32-C3 SuperMini | — | RISC-V, 160 MHz, native USB CDC |
| Display | ST7789 76×284 px | SPI (MOSI 6, SCLK 4, CS 5, DC 7, RST 8) | Variant P3, needs 3 TFT_eSPI patches |
| Backlight | LED cathode-driven | GPIO10 via NPN transistor | PWM dimming (LEDC CH0, 5 kHz, 8-bit) |
| Button A | Tact switch | GPIO2, INPUT_PULLUP | Short=test, Long=alarm threshold, Boot-hold=token reset |
| Button B | Tact switch | GPIO3, INPUT_PULLUP | Short=brightness, Long=mute toggle |
| Buzzer | Passive | GPIO0 via NPN transistor | PWM tones (LEDC CH1, variable freq, 8-bit) |
| Factory reset | BOOT switch | GPIO9, INPUT_PULLUP | Hold at power-on → full NVS erase |

### Pinout

```
GPIO 0  ──[1kΩ]──► NPN base        (buzzer, PWM CH1)
GPIO 2  ── tact A ──► GND          (INPUT_PULLUP)
GPIO 3  ── tact B ──► GND          (INPUT_PULLUP)
GPIO 4  ── SCLK
GPIO 5  ── CS
GPIO 6  ── MOSI
GPIO 7  ── DC
GPIO 8  ── RST
GPIO 9  ── BOOT button             (factory reset)
GPIO 10 ──[1kΩ]──► NPN base        (backlight, PWM CH0)
```

> **GPIO 0 caution:** GPIO0 is the boot-strapping pin. The NPN base resistor limits current so the pin is not pulled hard to GND during boot. Verified working with 1 kΩ base resistor.

---

## 3. Software Architecture

### 3.1 Build Environment

- **PlatformIO** with `espressif32 @ 6.9.0`
- **Framework:** Arduino-ESP32 (core 2.x, IDF 5.x based)
- **Partition:** Default OTA-compatible (`app0`/`app1` @ 1.25 MB each)
- **Flash usage:** ~76% (~994 KB of 1310 KB)
- **RAM usage:** ~13.4% (~44 KB of 328 KB)
- **Upload:** `--no-stub` required for native USB CDC

### 3.2 Key Build Flags

```ini
build_flags =
    -DCORE_DEBUG_LEVEL=0
    -DTFT_MISO=3
```

Do **not** add `ARDUINO_USB_CDC_ON_BOOT=1` — conflicts with TFT_eSPI.

### 3.3 Dependencies

| Library | Version | Purpose |
|---|---|---|
| `TFT_eSPI` | ^2.5.43 | Display driver (requires 3 manual patches) |
| `ArduinoJson` | — | JSON parsing (included for token sync tool) |

### 3.4 Module Map

```
src/
├── main.cpp              State machine, button handlers, overlay logic, alarm check
├── display/
│   ├── display.cpp/h     TFT init, PWM backlight (LEDC CH0)
│   ├── ui.cpp/h          All screen rendering (Pac-Man palette, 3D bars, badges)
│   └── pacman.cpp/h      Sprite animation, dot track, fraction-based positioning
├── hardware/
│   ├── buttons.cpp/h     Debounced INPUT_PULLUP buttons, short/long press detection
│   └── buzzer.cpp/h      PWM tones, boot melody, non-blocking alarm sequencer
├── network/
│   ├── api_client.cpp/h  HTTPS POST /v1/messages, TLS root CA, rate-limit header parsing
│   ├── api_task.cpp/h    FreeRTOS task (poll every 3 min, 429 backoff), mutex-guarded results
│   ├── web_config.cpp/h  Unified dashboard (/), token update, OTA flash (/update)
│   └── wifi_mgr.cpp/h    STA connection, AP mode with WPA2 + random password
└── storage/
    └── storage.cpp/h     NVS read/write via Preferences, fixed-size AppConfig
```

---

## 4. State Machine

```
                    ┌─────────────────┐
                    │     S_INIT      │
                    └────────┬────────┘
                             │
            ┌────────────────┴────────────────┐
            │ no config (ssid or apiKey empty) │ config OK
            ▼                                    ▼
    ┌───────────────┐                  ┌─────────────────┐
    │   S_AP_MODE   │                  │  S_CONNECTING   │
    │  (web server) │                  │ (WiFi connect)  │
    └───────┬───────┘                  └────────┬────────┘
            │                                   │
            │ user submits form                 │ connected
            │ → ESP.restart()                   ▼
            │                          ┌─────────────────┐
            │                          │    S_RUNNING    │
            │                          │  (main loop)    │
            │                          └────────┬────────┘
            │                                   │ WiFi drop
            │                                   ▼
            │                          ┌─────────────────┐
            │                          │    S_ERROR      │
            │                          │ (30s countdown) │
            │                          │  → ESP.restart()│
            │                          └─────────────────┘
            │                                   ▲
            └───────────────────────────────────┘
                         timeout
```

### State Details

| State | Behavior |
|---|---|
| **S_INIT** | Loads config from NVS. If valid → S_CONNECTING. Else → starts AP, shows password on screen, serves config portal. |
| **S_AP_MODE** | Blocks on `webConfigHandle()`. Web form at `/` collects SSID, pass, token. On submit → saves to NVS → restart. |
| **S_CONNECTING** | Attempts STA connection with 30 s timeout. On success → NTP sync, mDNS, starts API task → S_RUNNING. On fail → S_ERROR. |
| **S_RUNNING** | Non-blocking loop: handles buttons, buzzer alarm tick, checks async API results, updates UI/Pac-Man, serves web requests. |
| **S_ERROR** | Shows error message + 30-second countdown, then restarts (config preserved). |

---

## 5. Storage Schema (NVS)

Namespace: `claude-mon`

| Key | Type | Default | Description |
|---|---|---|---|
| `ssid` | String | `""` | Wi-Fi SSID (max 32 chars) |
| `pass` | String | `""` | Wi-Fi password (max 64 chars) |
| `apikey` | String | `""` | OAuth access token (max 512 chars) |
| `brightness` | U8 | `100` | Backlight PWM duty: 25, 50, 75, or 100 |
| `alarm_thr` | U8 | `80` | Alarm trigger threshold: 0 (off), 80, or 90 |
| `buzzer_mute` | Bool | `false` | Global buzzer mute state |

**Backward compatibility:** `storageLoad()` uses `getUChar`/`getBool` with defaults. Older firmware versions that only stored `ssid`/`pass`/`apikey` will seamlessly upgrade on first boot.

### `AppConfig` struct

```cpp
struct AppConfig {
    char    ssid[33];
    char    pass[65];
    char    apiKey[513];
    uint8_t brightness;   // 0-100
    uint8_t alarmThr;     // 0=off, 80, 90
    bool    buzzerMute;
};
```

> All strings are fixed-size `char[]` — no `String` objects in config to prevent heap fragmentation.

---

## 6. API Integration

### Endpoint

```
POST https://api.anthropic.com/v1/messages
Authorization: Bearer <token>
anthropic-version: 2023-06-01
Content-Type: application/json
```

### Request Body

```json
{"model":"claude-haiku-4-5-20251001","max_tokens":1,"messages":[{"role":"user","content":"."}]}
```

### Response: Rate-Limit Headers

The response body is ignored. Rate-limit data is read from HTTP headers (present even on HTTP 429 and HTTP 401):

| Header | Example | Meaning |
|---|---|---|
| `anthropic-ratelimit-unified-5h-utilization` | `0.0` | 5H utilization % (0–100) |
| `anthropic-ratelimit-unified-5h-reset` | `1779893400` | 5H reset Unix timestamp |
| `anthropic-ratelimit-unified-7d-utilization` | `25.0` | 7D utilization % (0–100) |
| `anthropic-ratelimit-unified-7d-reset` | `1780257600` | 7D reset Unix timestamp |

### Rate Limit Struct

```cpp
struct RateLimit {
    bool    valid   = false;
    float   util5h  = 0.0f;   // 0.0–1.0+
    float   util7d  = 0.0f;
    time_t  reset5hAt = 0;
    time_t  reset7dAt = 0;
    int     httpCode = 0;
};
```

### Token Hot-Swap

`apiTaskUpdateToken(const char* token)` updates the OAuth token in the running background task without rebooting. The task copies the shared token into a local buffer under the same mutex before each `apiFetch()`, so the new token is picked up on the next poll cycle.

### Polling Behavior

| Condition | Action |
|---|---|
| Normal | Poll every `API_POLL_MS` (180 000 ms = 3 min) |
| HTTP 429 | Backoff: add 5 minutes to next poll interval |
| HTTP 401 | UI shows `TOKEN WYGASL` screen; user must update token |
| TCP/SSL error (< 0) | Retry once after 2 s delay |
| Negative httpCode | Retry once only |

### TLS Security

- `WiFiClientSecure` with `setCACert(ANTHROPIC_ROOT_CA)`
- Embedded certificate: GTS Root R4 (Google Trust Services)
- **No** `setInsecure()` — certificate is pinned

---

## 7. UI Rendering

### Coordinate System

- Display: **284 × 76 px** (landscape, rotation 1)
- All coordinates assume `tft.setRotation(1)`

### Screen Zones

| Zone | Y Range | Content |
|---|---|---|
| Top bar | 0–11 | Left accent (blue), `CLAUDE·MAN` title, IP address right |
| Pac-Man track | 12–57 | Animated Pac-Man, dot track, 5H % + reset time (right) |
| Bottom bar | 58–71 | 7D badge, 7D %, reset/error, overlay text, WiFi/API dots, clock |
| Progress bar | 72–75 | 3D metallic 7D utilization bar |

### Pac-Man Animation

- `pacmanSetFraction(float f)` — 0.0 = left, 1.0 = right
- `pacmanTick()` — mouth toggle every 150 ms, non-blocking
- Dots are eaten (not drawn) based on fraction; 16 dots total
- Pac-Man color changes with utilization: yellow → orange → red

### Overlay

Temporary 1.5-second feedback text drawn in the bottom bar at `x=90, y=61` (between 7D data and WiFi dot). Cleared by next `uiShowMain()` call (triggered after timeout or API result).

---

## 8. Button Logic

### Debounce & Timing

```
Debounce:     50 ms (state must be stable)
Long press:   ≥ 1500 ms
Boot hold:    ≥ 3000 ms (token reset only)
```

### Event Detection

`buttonsGetEvent()` returns:
- `EVT_NONE` — no stable event
- `EVT_SHORT` — press → release within 1500 ms
- `EVT_LONG` — held for ≥ 1500 ms (fires once, then waits for release)

### Handler Matrix

| Button | Short Press | Long Press | Boot Hold |
|---|---|---|---|
| **A** (GPIO2) | `buzzerTestBeep()` | Cycle `alarmThr`: 80→90→0→80 | Reset only `apiKey` (keep WiFi) |
| **B** (GPIO3) | Cycle `brightness`: 25→50→75→100 | Toggle `buzzerMute` | — |

All changes are saved to NVS immediately via `storageSave(cfg)`.

---

## 9. Buzzer Logic

### PWM Configuration

- Channel: LEDC CH1
- Resolution: 8-bit
- Frequency: variable (set per tone via `ledcWriteTone()`)

### Functions

| Function | Blocking | Use Case |
|---|---|---|
| `buzzerPlayStart()` | Yes (≈ 600 ms) | Boot melody in `setup()` |
| `buzzerTestBeep()` | Yes (≈ 150 ms) | Button A short press |
| `buzzerSetAlarmLevel()` | No | Set target alarm pattern |
| `buzzerAlarmTick()` | No | Call every loop() iteration — drives state machine |

### Alarm Patterns (Non-blocking)

| Level | Trigger Condition | Freq | Tone | Pause |
|---|---|---|---|---|
| **LOW** | util ≥ threshold, < 90% | 800→600 Hz | 100 ms | 3000 ms |
| **HIGH** | util ≥ 90%, < 100% | 2000 Hz | 80 ms | 1500 ms |
| **CRITICAL** | util ≥ 100% | 2000 Hz | 200 ms | 400 ms |
| **NONE** | util < threshold or thr=0 | — | — | — |

### Mute Behavior

- `buzzerMute(true)` immediately silences active tones
- `buzzerSetAlarmLevel(ALARM_NONE)` is called on mute toggle to stop current pattern
- Mute state is persisted in NVS
- Test beep (`buzzerTestBeep()`) plays regardless of mute state (restores mute after)

---

## 10. Web Dashboard

Unified single-page dashboard served at `/` in both AP and STA mode.

### Endpoints

| Path | Method | Description |
|---|---|---|
| `/` | GET | Main dashboard (usage stats + token form + OTA upload) |
| `/save` | POST | Save Wi-Fi SSID + password + token (from AP mode) |
| `/token-save` | POST | Update OAuth token only (from STA mode); calls `apiTaskUpdateToken()` live — no reboot |
| `/update` | GET | OTA firmware upload form |
| `/update` | POST | Flash firmware via `Update.h` |

### Security

- AP mode: WPA2 with random 8-char alphanumeric password (no 0/O/I/1)
- STA mode: no auth on local network (assumes trusted LAN)
- Token update accepts any string and saves directly to NVS

### OTA Procedure

1. Build firmware: `pio run`
2. Open `http://claude-monitor.local/update`
3. Select `.pio/build/esp32-c3-supermini/firmware.bin`
4. Upload → device flashes and restarts automatically

---

## 11. Token Sync Tool

`tools/sync-token.py` pushes fresh OAuth tokens from `~/.claude/.credentials.json` to the device.

### Modes

| Mode | Mechanism | RAM Cost |
|---|---|---|
| **Watchdog** (default) | `watchdog` library monitors file via inotify/fsevents | Only when file changes |
| **Polling** (fallback) | Checks file hash every `CLAUDE_MON_INTERVAL` seconds | Continuous Python process |
| **Oneshot** (`--oneshot`) | Reads file once, pushes, exits | Zero (process exits) |

### systemd Integration

```bash
systemctl --user enable --now sync-token.path   # Path unit (recommended)
# or
systemctl --user enable --now sync-token-polling.service  # Continuous
```

Environment:
- `CLAUDE_MON_HOST` — default `claude-monitor.local`
- `CLAUDE_MON_INTERVAL` — default `600` (seconds)

---

## 12. Security Checklist

| Item | Status | Details |
|---|---|---|
| TLS certificate validation | ✅ | Embedded GTS Root R4, `setCACert()` |
| Insecure mode | ❌ Removed | No `setInsecure()` anywhere |
| Open AP | ❌ Removed | WPA2 with random password |
| HTTP body in PROGMEM | ✅ | API request body stored in flash |
| Fixed-size config | ✅ | No `String` in `AppConfig` |
| Overflow-safe timers | ✅ | `(int32_t)(millis() - last) >= interval` |
| Reentrant time functions | ✅ | `localtime_r()` instead of `localtime()` |
| Mutex-guarded API results | ✅ | `apiTaskGetResult()` under `xSemaphore` |

---

## 13. Known Constraints & Gotchas

1. **TFT_eSPI patches required** — Without the 3 manual patches (SPI_PORT, CGRAM offset, TFT_MISO dummy), display stays black.
2. **GPIO0 is boot-strapping** — Buzzer circuit must use base resistor so pin is not hard-pulled to GND at boot.
3. **USB CDC conflict** — Do not enable `ARDUINO_USB_CDC_ON_BOOT=1`; breaks TFT_eSPI compile.
4. **API rate limiting** — Anthropic rate-limits `POST /v1/messages`. 3-minute interval + 429 backoff keeps usage well within limits.
5. **Serial unavailable on USB** — Logs go to UART0 pins (GPIO20/21), not USB CDC.
6. **NVS wear** — Button settings are saved on every change. At ~100k write cycles, even 100 presses/day lasts ~2.7 years.
7. **Time drift** — NTP syncs once at connect. No periodic re-sync; acceptable for session-scale monitoring.

---

## 14. File Inventory

```
ai_session_limits/
├── include/
│   └── config.h                    Hardware pins, timing, defaults
├── src/
│   ├── main.cpp                    Setup, state machine, button handlers, alarm logic
│   ├── display/
│   │   ├── display.h               TFT_eSPI extern, init/backlight protos
│   │   ├── display.cpp             TFT init, PWM backlight (LEDC CH0)
│   │   ├── ui.h                    Screen rendering function prototypes
│   │   ├── ui.cpp                  All UI drawing (badges, bars, text, overlays)
│   │   ├── pacman.h                Pac-Man animation API
│   │   └── pacman.cpp              Sprite drawing, dot track, fraction logic
│   ├── hardware/
│   │   ├── buttons.h               Button enum, event enum, API
│   │   ├── buttons.cpp             Debounce, short/long press detection
│   │   ├── buzzer.h                Buzzer + alarm level enum, API
│   │   └── buzzer.cpp              PWM tones, melodies, non-blocking alarm sequencer
│   ├── network/
│   │   ├── api_client.h            RateLimit struct, apiFetch() prototype
│   │   ├── api_client.cpp          HTTPS client, TLS cert, JSON parsing
│   │   ├── api_task.h              FreeRTOS task API
│   │   ├── api_task.cpp            Background poll loop, mutex, 429 backoff
│   │   ├── web_config.h            Web server start/handle prototypes
│   │   ├── web_config.cpp          Dashboard HTML, handlers, OTA flash
│   │   ├── wifi_mgr.h              WiFi STA/AP prototypes
│   │   └── wifi_mgr.cpp            Connection management, AP with random pass
│   └── storage/
│       ├── storage.h               AppConfig struct, load/save/clear prototypes
│       └── storage.cpp             NVS read/write via Preferences
├── docs/
│   └── TFT_eSPI_ESP32C3_ST7789_76x284_fixes.md
├── tools/
│   ├── sync-token.py
│   ├── sync-token.path
│   ├── sync-token.service
│   └── sync-token-polling.service
├── platformio.ini
├── README.md
└── SESSION.md          ← this file
```

---

## 15. Quick Reference: Changing Defaults

Edit `include/config.h` and reflash:

```cpp
#define API_POLL_MS      180000       // 3 minutes (min 120000 recommended)
#define DEFAULT_BRIGHTNESS  100        // 25, 50, 75, or 100
#define DEFAULT_ALARM_THR   80         // 0, 80, or 90
```

---

## 16. Changelog

### 2026-05-26 — Hardware controls + PWM + Alarms
- Added Button A (GPIO2) and Button B (GPIO3) with debounce, short/long press
- Added passive buzzer on GPIO0 with PWM tones and 3 alarm levels
- Replaced binary backlight ON/OFF with LEDC PWM dimming (4 levels, 25–100%)
- Added NVS persistence for brightness, alarm threshold, and mute state
- Added boot-hold token reset (Button A, 3 s)
- Added UI overlay feedback for button actions
- Buzzer alarm is fully non-blocking (state machine in loop)

### Earlier
- Restored `POST /v1/messages` probe request (~1 token per poll) to get always-present rate-limit headers including reset timestamps
- Added FreeRTOS background API task with mutex-guarded results
- Added TLS root CA pinning (GTS Root R4)
- Added WPA2-protected AP mode with random password
- Refactored config from `String` to fixed `char[]` buffers
- Added unified Pac-Man themed web dashboard with OTA updates
- Added systemd token sync tools (path unit + oneshot service)
- Fixed E429 rate limit handling (180 s interval + 5 min backoff)
- Fixed `millis()` overflow in timers
- Fixed `localtime()` thread-safety (`localtime_r`)
