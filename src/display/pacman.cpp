#include "pacman.h"
#include "display.h"
#include "../../include/config.h"

// ─── Layout ──────────────────────────────────────────────────────────────────
static const int ROW_Y    = 12;
static const int ROW_H    = 46;
static const int PAC_CY   = ROW_Y + ROW_H / 2;  // 35
static const int PAC_R    = 12;
static const int DOT_R    = 3;
static const int PAC_X_MIN   = 16;
static const int PAC_X_MAX   = 234;
static const int PAC_TRACK_W = 248;
static const int DOT_FIRST   = PAC_X_MIN + PAC_R + 8;  // 36
static const int DOT_STEP    = 10;
static const int DOT_LAST    = 242;   // just before data area at x=248
static const int N_DOTS    = (DOT_LAST - DOT_FIRST) / DOT_STEP + 1;

// ─── Colors ──────────────────────────────────────────────────────────────────
// RGB565: R=bits[15:11] G=bits[10:5] B=bits[4:0]
static const uint16_t C_PAC_RIM   = 0xFE80; // dark gold  R248 G208 B0
static const uint16_t C_PAC_BODY  = 0xFFE0; // bright yellow (TFT_YELLOW)
static const uint16_t C_PAC_SHINE = 0xFFF9; // warm white R255 G255 B200
static const uint16_t C_SHADOW    = 0x2965; // very dark warm grey
static const uint16_t C_DOT       = 0xFFFF; // white
static const uint16_t C_DOT_SHINE = 0xCEFF; // blue-white specular

// ─── State ───────────────────────────────────────────────────────────────────
static float    _fraction   = 0.0f;
static bool     _mouthOpen  = true;
static uint32_t _lastToggle = 0;

// ─── Helpers ─────────────────────────────────────────────────────────────────
static int calcPacX() {
    return PAC_X_MIN + (int)(_fraction * (PAC_X_MAX - PAC_X_MIN));
}

static void drawDot(int dx) {
    // Shadow (down-right)
    tft.fillCircle(dx + 1, PAC_CY + 1, DOT_R, C_SHADOW);
    // White body
    tft.fillCircle(dx, PAC_CY, DOT_R, C_DOT);
    // Specular highlight (top-left)
    tft.fillCircle(dx - 1, PAC_CY - 1, 1, C_DOT_SHINE);
}

static void drawBody(int cx, bool open) {
    // Drop shadow (offset down-right, dark)
    tft.fillCircle(cx + 3, PAC_CY + 3, PAC_R - 1, C_SHADOW);

    // Outer rim (dark gold — gives edge depth)
    tft.fillCircle(cx, PAC_CY, PAC_R, C_PAC_RIM);

    // Main bright yellow body
    tft.fillCircle(cx, PAC_CY, PAC_R - 2, C_PAC_BODY);

    // Mouth cutout (open or closed)
    if (open) {
        tft.fillTriangle(
            cx,          PAC_CY,
            cx + PAC_R + 2, PAC_CY - 7,
            cx + PAC_R + 2, PAC_CY + 7,
            TFT_BLACK
        );
    }

    // Specular highlight — upper-left quadrant (3D illusion)
    tft.fillCircle(cx - 4, PAC_CY - 5, 3, C_PAC_SHINE);

    // Eye
    tft.fillCircle(cx - 3, PAC_CY - 7, 2, TFT_BLACK);
}

// ─── Public ──────────────────────────────────────────────────────────────────
void pacmanSetFraction(float f) {
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    _fraction = f;
}

void pacmanDraw() {
    int cx = calcPacX();

    tft.fillRect(0, ROW_Y, PAC_TRACK_W, ROW_H, TFT_BLACK);

    // Uneaten dots (right of Pacman)
    for (int i = 0; i < N_DOTS; i++) {
        int dx = DOT_FIRST + i * DOT_STEP;
        if (dx > cx + PAC_R) {
            drawDot(dx);
        }
    }

    drawBody(cx, _mouthOpen);
}

void pacmanTick() {
    uint32_t now = millis();
    if (now - _lastToggle < 300) return;

    _mouthOpen  = !_mouthOpen;
    _lastToggle = now;

    int cx = calcPacX();

    // Clear Pacman area (wider to cover shadow)
    int x0 = cx - PAC_R - 4;
    int w  = (PAC_R + 4) * 2 + 8;
    if (x0 < 0) x0 = 0;
    if (x0 + w > PAC_TRACK_W) w = PAC_TRACK_W - x0;
    tft.fillRect(x0, ROW_Y, w, ROW_H, TFT_BLACK);

    // Restore dots that were in the cleared area
    for (int i = 0; i < N_DOTS; i++) {
        int dx = DOT_FIRST + i * DOT_STEP;
        if (dx > cx + PAC_R && dx >= x0 && dx <= x0 + w) {
            drawDot(dx);
        }
    }

    drawBody(cx, _mouthOpen);
}
