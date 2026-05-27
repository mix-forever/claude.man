#include "ui.h"
#include "display.h"
#include "pacman.h"
#include "../../include/config.h"
#include <time.h>

// ─── Pacman palette (official arcade colors) ─────────────────────────────────
// Maze
static const uint16_t C_MAZE   = 0x211B;  // #2121DE  French Ultramarine maze walls
// Characters
static const uint16_t C_PAC    = 0xFFE0;  // #FFFF00  Digital Yellow — Pac-Man & dots
static const uint16_t C_INKY   = 0x07FF;  // #00FFFF  Aqua/Cyan      — Inky  → WiFi/IP
static const uint16_t C_PINKY  = 0xFDBF;  // #FFB8FF  Pastel Pink    — Pinky → API
static const uint16_t C_BLINKY = 0xF800;  // #FF0000  Digital Red    — Blinky → error
static const uint16_t C_CLYDE  = 0xFDAA;  // #FFB852  Pastel Orange  — Clyde → warning
// Neutral
static const uint16_t C_DIM    = 0x4208;  // dark grey (secondary labels)
static const uint16_t C_TRACK  = 0x18C3;  // very dark track background

// ─── Color helpers ───────────────────────────────────────────────────────────

static uint16_t dimColor(uint16_t c) {
    return (((c >> 11) & 0x1F) >> 1) << 11
         | (((c >>  5) & 0x3F) >> 1) <<  5
         | (( c        & 0x1F) >> 1);
}

static uint16_t brightColor(uint16_t c) {
    uint16_t r = (c >> 11) & 0x1F,  g = (c >> 5) & 0x3F,  b = c & 0x1F;
    return ((r + (0x1F - r) / 2) << 11)
         | ((g + (0x3F - g) / 2) <<  5)
         |  (b + (0x1F - b) / 2);
}

// Utilization → Pacman color: Pacman-yellow OK, Clyde-orange warn, Blinky-red crit
static uint16_t utilColor(float f) {
    if (f >= 0.9f) return C_BLINKY;
    if (f >= 0.7f) return C_CLYDE;
    return C_PAC;
}

// ─── Formatters ──────────────────────────────────────────────────────────────

static String fmtPct(float v) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%.0f%%", v * 100.0f);
    return String(buf);
}


static String secsToStr(int secs) {
    if (secs <= 0) return "0s";
    char buf[16];
    if      (secs >= 86400) snprintf(buf, sizeof(buf), "%dd%02dh", secs/86400, (secs%86400)/3600);
    else if (secs >= 3600)  snprintf(buf, sizeof(buf), "%dh%02dm", secs/3600,  (secs%3600)/60);
    else if (secs >= 60)    snprintf(buf, sizeof(buf), "%dm%02ds", secs/60,    secs%60);
    else                    snprintf(buf, sizeof(buf), "%ds",       secs);
    return String(buf);
}

// ─── 3D primitives ───────────────────────────────────────────────────────────

// Maze-tile badge: blue body with top-left bevel highlight, yellow text.
// Looks like a classic Pacman wall block.
static void drawBadge(int x, int y, const char* text) {
    int tw = strlen(text) * 6;
    int w  = tw + 8,  h = 12;
    // Drop shadow (dark blue, offset +1,+1)
    tft.fillRect(x + 1, y + 1, w, h, dimColor(C_MAZE));
    // Main body (maze blue)
    tft.fillRect(x,     y,     w, h, C_MAZE);
    // Top-left bevel: lit face of wall block
    tft.drawFastHLine(x,     y,     w,     brightColor(C_MAZE));
    tft.drawFastVLine(x,     y + 1, h - 1, brightColor(C_MAZE));
    // Bottom-right shadow edge: reinforces depth
    tft.drawFastHLine(x + 1, y + h - 1, w - 1, dimColor(C_MAZE));
    tft.drawFastVLine(x + w - 1, y + 1, h - 2, dimColor(C_MAZE));
    // Pacman-yellow text
    tft.setTextSize(1);
    tft.setTextColor(C_PAC, C_MAZE);
    tft.setCursor(x + 4, y + 2);
    tft.print(text);
}

// 3D dot: drop shadow + body + specular highlight
static void drawDot3D(int cx, int cy, int r, uint16_t color) {
    tft.fillCircle(cx + 1, cy + 1, r,  dimColor(color));    // shadow
    tft.fillCircle(cx,     cy,     r,  color);               // body
    tft.fillCircle(cx - 1, cy - 1, 1,  brightColor(color)); // specular
}

// 3D metallic bar: bright top highlight, solid fill ×2, dark shadow bottom
static void drawBar3D(int y, float f, uint16_t color) {
    uint16_t hi = brightColor(color),  sh = dimColor(color);
    int fw = (int)(f * DISPLAY_WIDTH);
    if (fw > DISPLAY_WIDTH) fw = DISPLAY_WIDTH;
    tft.fillRect(0, y, DISPLAY_WIDTH, 4, C_TRACK);
    if (fw > 0) {
        tft.drawFastHLine(0, y,     fw, hi);
        tft.drawFastHLine(0, y + 1, fw, color);
        tft.drawFastHLine(0, y + 2, fw, color);
        tft.drawFastHLine(0, y + 3, fw, sh);
    }
}

// ─── Config mode ─────────────────────────────────────────────────────────────

void uiShowConfigMode(const String& apSSID, const String& ip, const String& pass) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(1);

    tft.fillRect(0, 0, 3, DISPLAY_HEIGHT, C_MAZE);  // maze-blue left accent bar

    tft.setTextColor(C_PAC, TFT_BLACK);
    tft.setCursor(7, 4);
    tft.print("KONFIGURACJA");
    tft.drawFastHLine(3, 15, DISPLAY_WIDTH - 3, dimColor(C_MAZE));

    tft.setTextColor(C_DIM, TFT_BLACK);
    tft.setCursor(7, 20);
    tft.print("Siec: ");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.print(apSSID);

    tft.setTextColor(C_DIM, TFT_BLACK);
    tft.setCursor(7, 32);
    tft.print("URL:  ");
    tft.setTextColor(C_INKY, TFT_BLACK);
    tft.print("http://");
    tft.print(ip);

    if (pass.length() > 0) {
        tft.setTextColor(C_DIM, TFT_BLACK);
        tft.setCursor(7, 44);
        tft.print("Haslo: ");
        tft.setTextColor(C_PAC, TFT_BLACK);
        tft.print(pass);
    }

    tft.setTextColor(C_DIM, TFT_BLACK);
    tft.setCursor(7, 58);
    tft.print("Polacz sie z ta siecia WiFi");
}

// ─── Connecting ──────────────────────────────────────────────────────────────

void uiShowConnecting(const String& ssid) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(1);
    tft.fillRect(0, 0, 3, DISPLAY_HEIGHT, C_INKY);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(7, 10);
    tft.print("Laczenie z WiFi...");
    tft.setTextColor(C_INKY, TFT_BLACK);
    tft.setCursor(7, 26);
    tft.print(ssid);

    // 4 maze-tile blocks (static — connect is blocking)
    for (int i = 0; i < 4; i++) {
        tft.fillRect(97 + i * 20, 45, 14, 10, dimColor(C_DIM));
        tft.fillRect(96 + i * 20, 44, 14, 10, C_DIM);
    }
}

void uiShowMain(const RateLimit& rl, bool wifiOK, bool apiOK, int apiErrCode, const String& localIP) {
    float    u5h = rl.valid ? rl.util5h : 0.0f;
    float    u7d = rl.valid ? rl.util7d : 0.0f;
    uint16_t c5  = utilColor(u5h);
    uint16_t c7  = utilColor(u7d);

    // ── Top area (y=0..11, 12px) — REQ  TOK  IP ─────────────────────────────
    tft.fillRect(0, 0, DISPLAY_WIDTH, 12, TFT_BLACK);
    tft.fillRect(0, 0, 3, 12, C_MAZE);

    tft.setTextSize(1);
    {
        // "CLAUDE" in C_INKY, middle dot in C_CLYDE, "MAN" in C_PAC — centered
        // Total: "CLAUDE" 6ch + "." 1ch + "MAN" 3ch = 10ch = 60px
        int cx = (DISPLAY_WIDTH - 60) / 2;
        tft.setTextColor(C_INKY, TFT_BLACK);
        tft.setCursor(cx, 2);
        tft.print("CLAUDE");
        tft.setTextColor(C_CLYDE, TFT_BLACK);
        tft.print(".");
        tft.setTextColor(C_PAC, TFT_BLACK);
        tft.print("MAN");
    }

    // IP — top right
    if (localIP.length() > 0) {
        tft.setTextColor(C_DIM, TFT_BLACK);
        tft.setCursor(DISPLAY_WIDTH - (int)localIP.length() * 6 - 2, 2);
        tft.print(localIP);
    }

    // Separator line
    tft.drawFastHLine(3, 11, DISPLAY_WIDTH - 3, dimColor(C_MAZE));

    // ── 5H data (far right of Pac-Man strip, y=12..57 = 46px) ───────────────
    tft.fillRect(248, 12, DISPLAY_WIDTH - 248, 46, TFT_BLACK);

    tft.setTextSize(1);
    {
        // Two rows centered in 46px strip: 8+4+8=20px → start y=12+(46-20)/2=25
        String pct = rl.valid ? fmtPct(u5h) : "---%";
        tft.setTextColor(rl.valid ? c5 : TFT_WHITE, TFT_BLACK);
        tft.setCursor(281 - (int)pct.length() * 6, 25);
        tft.print(pct);

        if (rl.valid) {
            int secsLeft = (int)(rl.reset5hAt - time(nullptr));
            if (secsLeft > 0) {
                String rs = secsToStr(secsLeft);
                tft.setTextColor(TFT_WHITE, TFT_BLACK);
                tft.setCursor(281 - (int)rs.length() * 6, 37);
                tft.print(rs);
            }
        }
    }

    // ── Bottom bar (y=58..71) ────────────────────────────────────────────────
    tft.fillRect(0, 58, DISPLAY_WIDTH, 14, TFT_BLACK);
    tft.fillRect(0, 58, 3, 14, C_MAZE);    // maze-blue accent bar

    drawBadge(5, 59, "7D");

    tft.setTextSize(1);
    {
        String pct = rl.valid ? fmtPct(u7d) : "---%";
        tft.setTextColor(rl.valid ? c7 : TFT_WHITE, TFT_BLACK);
        tft.setCursor(32, 61);
        tft.print(pct);

        if (!apiOK && apiErrCode != 0) {
            char code[8];
            snprintf(code, sizeof(code), "E%d", apiErrCode);
            tft.setTextColor(C_BLINKY, TFT_BLACK);
            tft.setCursor(32 + (int)pct.length() * 6 + 6, 61);
            tft.print(code);
        } else if (rl.valid) {
            int secsLeft7 = (int)(rl.reset7dAt - time(nullptr));
            if (secsLeft7 > 0) {
                String rs = secsToStr(secsLeft7);
                tft.setTextColor(TFT_WHITE, TFT_BLACK);
                tft.setCursor(32 + (int)pct.length() * 6 + 6, 61);
                tft.print(rs);
            }
        }
    }

    // ● WiFi  (dot then label, both Inky-cyan when OK, Blinky-red when fail)
    uint16_t wCol = wifiOK ? C_INKY   : C_BLINKY;
    uint16_t aCol = apiOK  ? C_PINKY  : C_BLINKY;

    drawDot3D(170, 65, 4, wCol);
    tft.setTextColor(wCol, TFT_BLACK);
    tft.setCursor(178, 62);
    tft.print("WiFi");

    // ● API
    drawDot3D(211, 65, 4, aCol);
    tft.setTextColor(aCol, TFT_BLACK);
    tft.setCursor(219, 62);
    tft.print("API");

    // HH:MM
    time_t now2  = time(nullptr);
    struct tm t;
    if (localtime_r(&now2, &t) && t.tm_year > 70) {
        char tbuf[6];
        snprintf(tbuf, sizeof(tbuf), "%02d:%02d", t.tm_hour, t.tm_min);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setCursor(250, 62);
        tft.print(tbuf);
    }

    drawBar3D(72, u7d, c7);
}

// ─── Token expired ───────────────────────────────────────────────────────────

void uiShowTokenExpired(const String& localIP) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(1);
    tft.fillRect(0, 0, 3, DISPLAY_HEIGHT, C_BLINKY);

    tft.setTextColor(C_BLINKY, TFT_BLACK);
    tft.setCursor(7, 5);
    tft.print("TOKEN WYGASL (401)");
    tft.drawFastHLine(3, 16, DISPLAY_WIDTH - 3, dimColor(C_BLINKY));

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(7, 24);
    tft.print("Zaktualizuj token:");

    tft.setTextColor(C_INKY, TFT_BLACK);
    tft.setCursor(7, 36);
    if (localIP.length() > 0) {
        tft.print("http://");
        tft.print(localIP);
        tft.print("/token");
    } else {
        tft.print("http://claude-monitor.local/token");
    }

    tft.setTextColor(C_DIM, TFT_BLACK);
    tft.setCursor(7, 58);
    tft.print("lub: claude-monitor.local/token");
}

// ─── Error ───────────────────────────────────────────────────────────────────

void uiShowError(const String& msg, int restartSecs) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(1);
    tft.fillRect(0, 0, 3, DISPLAY_HEIGHT, C_BLINKY);  // Blinky-red accent bar

    tft.setTextColor(C_BLINKY, TFT_BLACK);
    tft.setCursor(7, 5);
    tft.print("! BLAD !");
    tft.drawFastHLine(3, 16, DISPLAY_WIDTH - 3, dimColor(C_BLINKY));

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(7, 24);
    tft.print(msg);

    if (restartSecs >= 0) {
        tft.setTextColor(C_DIM, TFT_BLACK);
        tft.setCursor(7, 56);
        tft.print("Restart za ");
        tft.setTextColor(C_CLYDE, TFT_BLACK);
        tft.print(restartSecs);
        tft.setTextColor(C_DIM, TFT_BLACK);
        tft.print("s");
    }
}
