# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

CLAUDE·MAN: ESP32-C3 SuperMini firmware (PlatformIO, Arduino framework) that polls the Anthropic usage API and shows Claude Code 5h/7d quota as a Pac-Man animation on a 76×284 ST7789 display. Plus a host-side Python tool (`tools/sync-token.py`) that pushes the OAuth token from `~/.claude/.credentials.json` to the device.

`README.md` is user-facing docs. `SESSION.md` (gitignored, but present on disk) is the detailed technical reference: NVS schema, API headers, alarm patterns, UI zones, changelog. Read it before non-trivial changes and update it when behavior changes.

## Commands

```bash
pio run                                        # build → .pio/build/esp32-c3-supermini/firmware.bin
pio run -t upload --upload-port /dev/ttyACM0   # flash (native USB CDC; port may change)
pio run -t clean
```

- There are no automated tests. Verification is build + flash + observe the device.
- Display work: `python3 tools/sim/sim.py /tmp/lcd 4` renders every screen to PNG through a mock TFT_eSPI (`tools/sim/`, needs g++ and Pillow). Look at `/tmp/lcd/all.png` before and after touching `src/display/`. Add new screens as scenes in `sim.cpp` + `sim.py`; new TFT primitives must be mirrored in `tools/sim/TFT_eSPI.h`.
- OTA alternative to flashing: upload `firmware.bin` at `http://claude-monitor.local/update`.
- `Serial` output is NOT on USB. It goes to UART0 pins GPIO20/21. Do not add `-DARDUINO_USB_CDC_ON_BOOT=1` or `-DSMOOTH_FONT=1` to fix that; both break the TFT_eSPI compile.
- Token sync tool: `python3 tools/sync-token.py [--oneshot]`, env `CLAUDE_MON_HOST`, `CLAUDE_MON_INTERVAL`. systemd units in `tools/` are user-level (`systemctl --user`). The script caches the device IP in `~/.cache/claude-man/host` and falls back to it when `.local` resolution fails.
- Formatting: `.clang-format` (Google style, 4-space indent, 100 cols, `PointerAlignment: Left`).

## Critical: TFT_eSPI must be hand-patched in libdeps

The display driver config is NOT in git. It lives in `.pio/libdeps/esp32-c3-supermini/TFT_eSPI/` and is gitignored. Three patches are required or the screen stays black:

1. `Processors/TFT_eSPI_ESP32_C3.h`: `#define SPI_PORT 2` (IDF 5.x register base; `SPI2_HOST` = 1 resolves to address 0).
2. `TFT_Drivers/ST7789_Rotation.h`: `else if(_init_width == 76)` blocks with `colstart=18, rowstart=82` in rotation cases 1 and 3.
3. `User_Setup.h`: full pin config (`ST7789_DRIVER`, `TFT_WIDTH 76`, `TFT_HEIGHT 284`, MOSI 6, MISO 3 dummy, SCLK 4, CS 5, DC 7, RST 8).

Full rationale and exact lines: `docs/TFT_eSPI_ESP32C3_ST7789_76x284_fixes.md` (Polish). If `.pio/` is deleted, the platform is re-installed, or TFT_eSPI is bumped, re-apply these. Verify with:

```bash
grep -n "define SPI_PORT" .pio/libdeps/esp32-c3-supermini/TFT_eSPI/Processors/TFT_eSPI_ESP32_C3.h
grep -n "_init_width == 76" .pio/libdeps/esp32-c3-supermini/TFT_eSPI/TFT_Drivers/ST7789_Rotation.h
```

Keep `platform = espressif32@6.9.0` pinned. Unpinned resolves to the pioarduino fork and the build fails. Keep `upload_speed = 115200` and `--no-stub`; faster speeds drop the USB CDC port mid-flash.

## Architecture

**Two execution contexts.** `loop()` in `src/main.cpp` runs a state machine (`S_INIT → S_AP_MODE | S_CONNECTING → S_RUNNING → S_ERROR`). Boot-time STA failures are counted in NVS (`wifi_fail`); at `WIFI_MAX_FAILS` the device enters AP setup with the SSID prefilled and retries after `AP_RETRY_MS`. A separate FreeRTOS task (`src/network/api_task.cpp`) does the HTTPS poll every `API_POLL_MS` so TLS never blocks the UI. All shared state between them (`RateLimit`, ok flag, HTTP code, token) is behind one mutex; `main.cpp` only touches it via `apiTaskHasResult()` / `apiTaskGetResult()`. `apiTaskUpdateToken()` swaps the token and wakes the task via `xTaskNotifyGive`, which is how the web dashboard updates the token without a reboot.

**API probe.** `api_client.cpp` sends `POST /v1/messages` with `max_tokens: 1` and ignores the body. Everything comes from `anthropic-ratelimit-unified-{5h,7d}-{utilization,reset}` headers, which are present even on 401/429. `RateLimit.valid` (headers parsed) is deliberately separate from `ok` (HTTP 200): a 401 still yields valid numbers but must show the token-expired screen. TLS is `setInsecure()` on purpose; cert pinning was reverted after Google rotated the GTS chain (see SESSION.md §13). Parsed headers are stored even on 429/401 (`valid` true, `ok` false). An empty token makes the task report 401 without a request.

**Everything in `S_RUNNING` is non-blocking.** Buttons (`buttons.cpp`) are polled with debounce and return `EVT_SHORT`/`EVT_LONG`; the buzzer alarm (`buzzer.cpp`) is a tick-driven sequencer; Pac-Man mouth animation is `pacmanTick()`. Only `buzzerPlayStart()`/`buzzerTestBeep()` block, and only for ms. The alarm is one-shot: `checkAlarm()` in `main.cpp` runs every loop iteration, fires once on threshold crossing, silences after `ALARM_ONE_SHOT_MS`, re-arms only after utilization drops below threshold.

**UI is zone-based, partial redraw.** `ui.cpp` draws into fixed Y bands on the 284×76 rotated display: top bar 0–11, Pac-Man corridor 12–57 (x 0–239, walls at rows 13–15 and 54–56) with the 5H data column at x 242–283, bottom bar 58–71, 7D progress bar 72–75. `pacman.cpp` owns the corridor: Pac-Man position = 5h fraction, Blinky = alarm threshold (`pacmanSetThreshold`), `pacmanTick()` redraws only the interior span around the sprites. Sprites are lit per pixel (`shade()`, constants `LX/LY/LZ`, `AMBIENT`, `DIFFUSE`, `SPEC_*`), rendered into a RAM canvas and sent with one `pushImage()` per sprite; never go back to clear-then-draw on the panel, it flickers. Tune the look in the simulator, not by eye on the device. `uiShowMain()` is the full redraw; `uiRefreshClock()` / `uiRefreshCountdowns()` update every minute independent of the API poll; `uiDrawOverlay()` writes 1.5 s feedback text into the bottom bar at x=90 and `redrawCurrentScreen()` in `main.cpp` clears it. `main.cpp` never draws on `tft` directly. While `tokenScreen` is set (401 or no token), the Pac-Man tick, clock refresh and overlay restore are suppressed so the full-screen message survives. New UI elements must not overlap these zones or the overlay slot.

**Config is a fixed-size struct.** `AppConfig` in `storage.h` uses `char[]` buffers, never `String`, to avoid heap fragmentation; NVS namespace is `claude-mon`. `storageLoad()` returns true when an SSID exists; an empty token is a valid state (device connects and shows `BRAK TOKENU`). Button handlers persist through `storageSaveSettings()`, which writes only brightness/alarm/mute, because the `main.cpp` copy of the token goes stale after a web update. New persisted settings need a default in `config.h`, a `getX(key, default)` in `storageLoad()`, a `putX` in both save functions, and initialization in `handleSave()` in `web_config.cpp`.

**Web server** (`web_config.cpp`) serves the dashboard at `/` in STA mode and the setup form in AP mode (template placeholders filled in `handleRoot()`). `GET /status` returns JSON assembled from `DeviceStatus`, which `main.cpp` fills through the provider registered with `webConfigSetStatusProvider()`. `/save` (AP: full config, then restart), `/token-save` (STA: token only, live hot-swap), `/update?size=N` (OTA via `Update.h`; the dashboard JS posts via XHR and shows progress). HTML and JS are inline in the .cpp as raw string literals; the shared CSS lives in a single-line macro (a macro cannot hold a multi-line raw string). All pages must use `HEAD_START`, `CSS` and `LOGO` so they stay consistent. OTA progress reaches the display through the callback set with `webConfigSetOtaProgress()`; the upload blocks the main loop, so the callback must stay cheap.

## Conventions and gotchas

- All `millis()` timers use the overflow-safe form `(int32_t)(millis() - last) >= interval`. Keep it.
- Use `localtime_r`, not `localtime`, for time formatting (established in `ui.cpp`).
- GPIO0 (buzzer) is a boot-strapping pin; GPIO9 is the BOOT button used for factory reset; GPIO3 doubles as button B and the dummy `TFT_MISO`. Don't reassign these without reading the pin table in the docs file.
- Pins, timings, poll interval, model, timezone all live in `include/config.h`. Change them there, not inline.
- `wifiConnectSTA()` disables Wi-Fi modem-sleep on purpose: with it on, the ESP32 drops multicast and `claude-monitor.local` stops resolving from wired hosts. Don't re-enable it.
- Code comments and on-device strings are a mix of Polish and English (e.g. `TOKEN WYGASL`, `WiFi rozlaczone`). Match the surrounding file; on-device strings must be ASCII (no diacritics) because the TFT font has none.
- LEDC channels: 0 = backlight (5 kHz), 1 = buzzer (variable via `ledcWriteTone`). Pick 2+ for anything new.
- Bump `FW_VERSION` in `config.h` on user-visible changes; it is shown in the dashboard footer.
- Flash is ~78% used. Adding libraries (ArduinoJson was removed as unused) or large fonts can overflow the OTA partition.
