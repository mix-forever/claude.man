#include "pacman.h"
#include "display.h"
#include "../../include/config.h"
#include <math.h>
#include <string.h>

// ─── Layout ──────────────────────────────────────────────────────────────────
static const int ROW_Y     = 12;
static const int ROW_H     = 46;
static const int TRACK_W   = 240;             // x 0..239, data column starts at 242
static const int PAC_CY    = ROW_Y + ROW_H / 2;  // 35
static const int PAC_R     = 13;              // 27 px disc (arcade 13x13 sprite, scaled)
static const int IN_Y0     = 16;              // corridor interior rows (between walls)
static const int IN_Y1     = 53;
static const int PAC_X_MIN = 21;
static const int PAC_X_MAX = 222;
static const int DOT_FIRST = 40;
static const int DOT_STEP  = 12;
static const int DOT_LAST  = 232;
static const int N_DOTS    = (DOT_LAST - DOT_FIRST) / DOT_STEP + 1;   // 17
static const int DOT_SZ    = 4;               // arcade pellets are small squares
static const int GHOST_R   = 13;              // dome radius; ghost is 27 wide, 27 tall
static const int GHOST_TOP = PAC_CY - GHOST_R;   // 22
static const int GHOST_BOT = PAC_CY + GHOST_R;   // 48

// ─── Colors (arcade palette) ─────────────────────────────────────────────────
static const uint16_t C_PAC     = 0xFFE0;  // #FFFF00 Pac-Man
static const uint16_t C_MAZE    = 0x211B;  // #2121DE maze walls
static const uint16_t C_PELLET  = 0xFDD2;  // #FFB897 pellets
static const uint16_t C_BLINKY  = 0xF800;  // #FF0000
static const uint16_t C_EYE     = 0xFFFF;
static const uint16_t C_PUPIL   = 0x001F;  // #0000FF
static const uint16_t C_OVER    = 0xF800;

// ─── Shading ─────────────────────────────────────────────────────────────────
// Simple Lambert + specular lighting on a sphere: key light from the upper
// left, ambient floor so the shadow side stays saturated, small highlight.
static const float LX = -0.45f, LY = -0.55f, LZ = 0.85f;   // normalized below
static const float AMBIENT = 0.58f, DIFFUSE = 0.55f, SPEC_POW = 14.0f, SPEC_GAIN = 0.70f;

static uint16_t pack565(float r, float g, float b) {
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
    if (r < 0) r = 0;     if (g < 0) g = 0;     if (b < 0) b = 0;
    return ((uint16_t)r >> 3) << 11 | ((uint16_t)g >> 2) << 5 | ((uint16_t)b >> 3);
}

// nx, ny, nz: surface normal (unit). Returns lit colour of base (r,g,b).
static uint16_t shade(float nx, float ny, float nz, float br, float bg, float bb) {
    static const float len = sqrtf(LX * LX + LY * LY + LZ * LZ);
    float lx = LX / len, ly = LY / len, lz = LZ / len;
    float ndl = nx * lx + ny * ly + nz * lz;
    if (ndl < 0) ndl = 0;
    float k = AMBIENT + DIFFUSE * ndl;
    // Blinn half-vector with the viewer on +z
    float hx = lx, hy = ly, hz = lz + 1.0f;
    float hl = sqrtf(hx * hx + hy * hy + hz * hz);
    float ndh = (nx * hx + ny * hy + nz * hz) / hl;
    float sp = ndh > 0 ? powf(ndh, SPEC_POW) * SPEC_GAIN : 0;
    return pack565(br * k + (255 - br * k) * sp,
                   bg * k + (255 - bg * k) * sp,
                   bb * k + (255 - bb * k) * sp);
}

// ─── Off-screen sprite canvas ────────────────────────────────────────────────
// Sprites are rendered into RAM and sent with one pushImage() per frame, so
// every pixel is written exactly once — no clear-then-draw flicker on the
// panel, and far fewer SPI transactions than per-run line drawing.
static const int SPR_W = 2 * GHOST_R + 1;   // 27 — both sprites fit
static const int SPR_H = 2 * GHOST_R + 2;   // 28 — ghost skirt + 1 row
static uint16_t  g_spr[SPR_W * SPR_H];

struct Canvas {
    int w, h;
    uint16_t* px;
    void clear() { memset(px, 0, (size_t)w * h * sizeof(uint16_t)); }
    void set(int x, int y, uint16_t c) { if (x >= 0 && y >= 0 && x < w && y < h) px[y * w + x] = c; }
    void hline(int x, int y, int len, uint16_t c) { for (int i = 0; i < len; i++) set(x + i, y, c); }
    void rect(int x, int y, int ww, int hh, uint16_t c) { for (int j = 0; j < hh; j++) hline(x, y + j, ww, c); }
    void circle(int x0, int y0, int r, uint16_t c) {   // TFT_eSPI::fillCircle coverage
        int x = 0, dx = 1, dy = r + r, p = -(r >> 1);
        hline(x0 - r, y0, dy + 1, c);
        while (x < r) {
            if (p >= 0) { hline(x0 - x, y0 + r, dx, c); hline(x0 - x, y0 - r, dx, c); dy -= 2; p -= dy; r--; }
            dx += 2; p += dx; x++;
            hline(x0 - r, y0 + x, dy + 1, c); hline(x0 - r, y0 - x, dy + 1, c);
        }
    }
    void tri(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {   // Adafruit fillTriangle
        int a, b, y, last, t;
        if (y0 > y1) { t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
        if (y1 > y2) { t = y2; y2 = y1; y1 = t; t = x2; x2 = x1; x1 = t; }
        if (y0 > y1) { t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
        if (y0 == y2) {
            a = b = x0;
            if (x1 < a) a = x1; else if (x1 > b) b = x1;
            if (x2 < a) a = x2; else if (x2 > b) b = x2;
            hline(a, y0, b - a + 1, c); return;
        }
        int dx01 = x1 - x0, dy01 = y1 - y0, dx02 = x2 - x0, dy02 = y2 - y0, dx12 = x2 - x1, dy12 = y2 - y1, sa = 0, sb = 0;
        last = (y1 == y2) ? y1 : y1 - 1;
        for (y = y0; y <= last; y++) {
            a = x0 + sa / dy01; b = x0 + sb / dy02; sa += dx01; sb += dx02;
            if (a > b) { t = a; a = b; b = t; }
            hline(a, y, b - a + 1, c);
        }
        sa = dx12 * (y - y1); sb = dx02 * (y - y0);
        for (; y <= y2; y++) {
            a = x1 + sa / dy12; b = x0 + sb / dy02; sa += dx12; sb += dx02;
            if (a > b) { t = a; a = b; b = t; }
            hline(a, y, b - a + 1, c);
        }
    }
    void push(int x, int y) { tft.pushImage(x, y, w, h, px); }
};

// Soft contact shadow on the corridor floor under a sprite (static, drawn
// by pacmanDraw() only — the sprites never overlap it)
static void drawFloorShadow(int cx, int halfW) {
    static const uint16_t C_SH1 = 0x2104, C_SH2 = 0x1082;   // dark greys
    tft.drawFastHLine(cx - halfW + 4, IN_Y1 - 2, 2 * halfW - 8, C_SH1);
    tft.drawFastHLine(cx - halfW + 1, IN_Y1 - 1, 2 * halfW - 2, C_SH2);
    tft.drawFastHLine(cx - halfW + 6, IN_Y1,     2 * halfW - 12, C_SH2);
}

// ─── State ───────────────────────────────────────────────────────────────────
static float    _fraction   = 0.0f;
static float    _threshold  = 0.0f;
static uint8_t  _mouthFrame = 2;       // 0 closed, 1 half, 2 wide
static int8_t   _mouthDir   = -1;
static uint8_t  _ghostFrame = 0;       // skirt wiggle
static uint32_t _lastToggle = 0;

// ─── Helpers ─────────────────────────────────────────────────────────────────
static int xForFraction(float f) {
    return PAC_X_MIN + (int)(f * (PAC_X_MAX - PAC_X_MIN) + 0.5f);
}
static int calcPacX()   { return xForFraction(_fraction); }
static bool ghostOn()   { return true; }   // Blinky is always on the track
// Alarm on: Blinky waits at the threshold and chases once it is crossed.
// Alarm off: he just waits at the end of the corridor.
static bool chasing()   { return _threshold > 0.0f && _fraction >= _threshold; }
static int  calcGhostX() {
    int cx = calcPacX();
    if (!chasing()) {
        // Waits at the threshold; backs off so the sprites never overlap
        // (Pac-Man nudges him along until the alarm fires).
        int gx = xForFraction(_threshold > 0.0f ? _threshold : 1.0f);
        int minGx = cx + PAC_R + GHOST_R + 2;
        if (gx < minGx) gx = minGx;
        if (gx > TRACK_W - 7 - GHOST_R) gx = TRACK_W - 7 - GHOST_R;   // corridor end
        return gx;
    }
    int gx = cx - PAC_R - GHOST_R - 3;   // right behind Pac-Man
    if (gx < GHOST_R + 6) gx = GHOST_R + 6;
    return gx;
}

static void drawPellet(int dx) {
    tft.fillRect(dx - DOT_SZ / 2, PAC_CY - DOT_SZ / 2, DOT_SZ, DOT_SZ, C_PELLET);
}

// Pac-Man: shaded yellow sphere with the arcade wedge mouth cut out to the
// right, rendered off-screen and pushed in one go.
static void drawPacman(int cx, uint8_t frame) {
    static const float TAN[3] = { 0.0f, 0.4663f, 1.0f };   // closed, 25°, 45°
    const int R = PAC_R, D = 2 * R + 1;
    Canvas cv{ D, D, g_spr };
    cv.clear();
    for (int dy = -R; dy <= R; dy++) {
        int w = (int)sqrtf((float)(R * R - dy * dy));
        int right = w;
        if (frame != 0) {
            int mouthEdge = (int)(fabsf((float)dy) / TAN[frame]);
            if (mouthEdge < w) right = mouthEdge;
        }
        for (int dx = -w; dx <= right; dx++) {
            float nx = dx / (float)R, ny = dy / (float)R, n2 = nx * nx + ny * ny;
            float nz = n2 < 1.0f ? sqrtf(1.0f - n2) : 0.0f;
            cv.set(dx + R, dy + R, shade(nx, ny, nz, 255, 255, 0));
        }
    }
    cv.push(cx - R, PAC_CY - R);
}

// Classic ghost: shaded dome + cylinder body + scalloped skirt, eyes looking
// toward Pac-Man. Rendered off-screen (27x28) and pushed in one go.
static void drawGhost(int gx, uint16_t body, uint8_t frame, bool lookLeft) {
    (void)body;   // Blinky red, lit per pixel
    const float br = 255, bg = 0, bb = 0;
    const int R = GHOST_R;
    Canvas cv{ SPR_W, SPR_H, g_spr };
    cv.clear();
    // Dome: upper half of a sphere, centre (R, R)
    for (int dy = -R; dy <= 0; dy++) {
        int w = (int)sqrtf((float)(R * R - dy * dy));
        for (int dx = -w; dx <= w; dx++) {
            float nx = dx / (float)R, ny = dy / (float)R, n2 = nx * nx + ny * ny;
            float nz = n2 < 1.0f ? sqrtf(1.0f - n2) : 0.0f;
            cv.set(dx + R, dy + R, shade(nx, ny, nz, br, bg, bb));
        }
    }
    // Body + skirt: vertical cylinder (normal varies with x only), rows R+1 .. 2R+1
    int skirtBottom = 2 * R + 1;          // canvas row of the skirt's lowest pixels
    for (int dx = -R; dx <= R; dx++) {
        float nx = dx / (float)R, nz = sqrtf(1.0f - nx * nx);
        uint16_t c = shade(nx, 0.15f, nz, br, bg, bb);
        for (int y = R + 1; y <= skirtBottom; y++) cv.set(dx + R, y, c);
    }
    // Skirt notches: three scallops; the two frames shift them for a wiggle.
    int off = frame ? 2 : 0;
    for (int i = 0; i < 3; i++) {
        int nx = 4 + off + i * 9;
        cv.tri(nx - 3, skirtBottom + 1, nx + 3, skirtBottom + 1, nx, skirtBottom - 3, TFT_BLACK);
    }
    if (frame) {   // wiggle: notch the outer corners on the alternate frame
        cv.tri(-1, skirtBottom + 1, 2, skirtBottom + 1, -1, skirtBottom - 2, TFT_BLACK);
        cv.tri(2 * R - 2, skirtBottom + 1, 2 * R + 1, skirtBottom + 1, 2 * R + 1, skirtBottom - 2, TFT_BLACK);
    }
    // Eyes
    int ey = 10;
    int look = lookLeft ? -2 : 2;
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        int ex = R + sgn * 5;
        cv.circle(ex, ey, 3, C_EYE);
        cv.rect(ex - 3, ey - 1, 7, 3, C_EYE);        // slightly oval
        cv.rect(ex + look - 1, ey, 3, 3, C_PUPIL);   // pupil toward Pac-Man
    }
    cv.push(gx - R, GHOST_TOP);
}

static void drawWalls() {
    // Double outline like the arcade maze: outer and inner rounded rectangles
    tft.drawRoundRect(2, ROW_Y + 1, TRACK_W - 4, ROW_H - 2, 5, C_MAZE);
    tft.drawRoundRect(4, ROW_Y + 3, TRACK_W - 8, ROW_H - 6, 3, C_MAZE);
}

static void drawPelletsIn(int x0, int x1, int cx) {
    for (int i = 0; i < N_DOTS; i++) {
        int dx = DOT_FIRST + i * DOT_STEP;
        if (dx > cx + PAC_R && dx + DOT_SZ >= x0 && dx - DOT_SZ <= x1) drawPellet(dx);
    }
}

static void drawGameOver(int cx) {
    // Fits in the eaten stretch left of Pac-Man; only meaningful at 100 %
    int x = (cx - PAC_R - 6 - 9 * 6) / 2;
    if (x < 8) x = 8;
    tft.setTextSize(1);
    tft.setTextColor(C_OVER, TFT_BLACK);
    tft.setCursor(x, PAC_CY - 4);
    tft.print("GAME OVER");
}

// ─── Public ──────────────────────────────────────────────────────────────────
void pacmanSetFraction(float f) {
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    _fraction = f;
}

void pacmanSetThreshold(float thr) {
    if (thr < 0.0f) thr = 0.0f;
    if (thr > 1.0f) thr = 1.0f;
    _threshold = thr;
}

void pacmanDraw() {
    int cx = calcPacX();
    tft.fillRect(0, ROW_Y, TRACK_W, ROW_H, TFT_BLACK);
    drawWalls();
    drawPelletsIn(0, TRACK_W, cx);
    if (_fraction >= 1.0f) drawGameOver(cx);
    int gx = calcGhostX();
    drawFloorShadow(gx, GHOST_R);
    drawFloorShadow(cx, PAC_R);
    drawGhost(gx, C_BLINKY, _ghostFrame, !chasing());
    drawPacman(cx, _mouthFrame);
}

void pacmanTick() {
    uint32_t now = millis();
    if (now - _lastToggle < 120) return;
    _lastToggle = now;

    // Mouth cycles wide → half → closed → half → wide (arcade chomp)
    if (_mouthFrame == 0) _mouthDir = 1;
    else if (_mouthFrame == 2) _mouthDir = -1;
    _mouthFrame += _mouthDir;
    _ghostFrame ^= 1;

    // Positions only change through pacmanDraw(); here we just replace the
    // sprite pixels in place — no clearing, so nothing flickers.
    int cx = calcPacX();
    int gx = calcGhostX();
    drawGhost(gx, C_BLINKY, _ghostFrame, !chasing());
    drawPacman(cx, _mouthFrame);
}
