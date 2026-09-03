// Host-side mock of TFT_eSPI: same primitives, same GLCD font, renders to a
// 16-bit framebuffer that sim.cpp dumps to disk. Drawing algorithms mirror
// TFT_eSPI (Adafruit-derived) so output matches the real panel pixel-for-pixel.
#pragma once
#include "Arduino.h"
#include <vector>
#include <algorithm>

#define TFT_BLACK   0x0000
#define TFT_WHITE   0xFFFF
#define TFT_YELLOW  0xFFE0
#define TFT_RED     0xF800
#define TFT_BLUE    0x001F
#define TFT_CYAN    0x07FF

extern const unsigned char font[];

class TFT_eSPI {
public:
    int W = 284, H = 76;
    std::vector<uint16_t> fb;
    int32_t cx = 0, cy = 0;
    uint16_t fg = TFT_WHITE, bg = TFT_BLACK;
    uint8_t  size = 1;

    void init() { fb.assign(W * H, 0); }
    void setRotation(uint8_t) {}
    int32_t width() const { return W; }
    int32_t height() const { return H; }

    void drawPixel(int32_t x, int32_t y, uint32_t c) {
        if (x < 0 || y < 0 || x >= W || y >= H) return;
        fb[y * W + x] = (uint16_t)c;
    }
    void setSwapBytes(bool) {}
    void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t* data) {
        for (int32_t j = 0; j < h; j++) for (int32_t i = 0; i < w; i++) drawPixel(x + i, y + j, data[j * w + i]);
    }
    void fillScreen(uint32_t c) { std::fill(fb.begin(), fb.end(), (uint16_t)c); }
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
        for (int32_t j = y; j < y + h; j++) for (int32_t i = x; i < x + w; i++) drawPixel(i, j, c);
    }
    void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) {
        drawFastHLine(x, y, w, c); drawFastHLine(x, y + h - 1, w, c);
        drawFastVLine(x, y, h, c); drawFastVLine(x + w - 1, y, h, c);
    }
    void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t c) { for (int32_t i = 0; i < w; i++) drawPixel(x + i, y, c); }
    void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t c) { for (int32_t j = 0; j < h; j++) drawPixel(x, y + j, c); }

    // TFT_eSPI::fillCircle (midpoint, identical scanline coverage)
    void fillCircle(int32_t x0, int32_t y0, int32_t r, uint32_t c) {
        int32_t x = 0, dx = 1, dy = r + r, p = -(r >> 1);
        drawFastHLine(x0 - r, y0, dy + 1, c);
        while (x < r) {
            if (p >= 0) {
                drawFastHLine(x0 - x, y0 + r, dx, c);
                drawFastHLine(x0 - x, y0 - r, dx, c);
                dy -= 2; p -= dy; r--;
            }
            dx += 2; p += dx; x++;
            drawFastHLine(x0 - r, y0 + x, dy + 1, c);
            drawFastHLine(x0 - r, y0 - x, dy + 1, c);
        }
    }
    void drawCircle(int32_t x0, int32_t y0, int32_t r, uint32_t c) {
        int32_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r;
        drawPixel(x0, y0 + r, c); drawPixel(x0, y0 - r, c); drawPixel(x0 + r, y0, c); drawPixel(x0 - r, y0, c);
        while (x < y) {
            if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
            x++; ddF_x += 2; f += ddF_x;
            drawPixel(x0 + x, y0 + y, c); drawPixel(x0 - x, y0 + y, c); drawPixel(x0 + x, y0 - y, c); drawPixel(x0 - x, y0 - y, c);
            drawPixel(x0 + y, y0 + x, c); drawPixel(x0 - y, y0 + x, c); drawPixel(x0 + y, y0 - x, c); drawPixel(x0 - y, y0 - x, c);
        }
    }
    void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t c) {
        int32_t a, b, y, last;
        if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }
        if (y1 > y2) { std::swap(y2, y1); std::swap(x2, x1); }
        if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }
        if (y0 == y2) {
            a = b = x0;
            if (x1 < a) a = x1; else if (x1 > b) b = x1;
            if (x2 < a) a = x2; else if (x2 > b) b = x2;
            drawFastHLine(a, y0, b - a + 1, c); return;
        }
        int32_t dx01 = x1 - x0, dy01 = y1 - y0, dx02 = x2 - x0, dy02 = y2 - y0, dx12 = x2 - x1, dy12 = y2 - y1, sa = 0, sb = 0;
        last = (y1 == y2) ? y1 : y1 - 1;
        for (y = y0; y <= last; y++) {
            a = x0 + sa / dy01; b = x0 + sb / dy02; sa += dx01; sb += dx02;
            if (a > b) std::swap(a, b);
            drawFastHLine(a, y, b - a + 1, c);
        }
        sa = dx12 * (y - y1); sb = dx02 * (y - y0);
        for (; y <= y2; y++) {
            a = x1 + sa / dy12; b = x0 + sb / dy02; sa += dx12; sb += dx02;
            if (a > b) std::swap(a, b);
            drawFastHLine(a, y, b - a + 1, c);
        }
    }
    void drawCircleHelper(int32_t x0, int32_t y0, int32_t r, uint8_t corner, uint32_t c) {
        int32_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r;
        while (x < y) {
            if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
            x++; ddF_x += 2; f += ddF_x;
            if (corner & 0x4) { drawPixel(x0 + x, y0 + y, c); drawPixel(x0 + y, y0 + x, c); }
            if (corner & 0x2) { drawPixel(x0 + x, y0 - y, c); drawPixel(x0 + y, y0 - x, c); }
            if (corner & 0x8) { drawPixel(x0 - y, y0 + x, c); drawPixel(x0 - x, y0 + y, c); }
            if (corner & 0x1) { drawPixel(x0 - y, y0 - x, c); drawPixel(x0 - x, y0 - y, c); }
        }
    }
    void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) {
        drawFastHLine(x + r, y, w - r - r, c); drawFastHLine(x + r, y + h - 1, w - r - r, c);
        drawFastVLine(x, y + r, h - r - r, c); drawFastVLine(x + w - 1, y + r, h - r - r, c);
        drawCircleHelper(x + r, y + r, r, 1, c); drawCircleHelper(x + w - r - 1, y + r, r, 2, c);
        drawCircleHelper(x + w - r - 1, y + h - r - 1, r, 4, c); drawCircleHelper(x + r, y + h - r - 1, r, 8, c);
    }
    void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c) {
        int32_t dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
        while (true) { drawPixel(x0, y0, c); if (x0 == x1 && y0 == y1) break; int32_t e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; } }
    }

    // ── Text (GLCD font 1 only) ──
    void setTextSize(uint8_t s) { size = s ? s : 1; }
    void setTextColor(uint16_t f) { fg = f; bg = f; }
    void setTextColor(uint16_t f, uint16_t b) { fg = f; bg = b; }
    void setCursor(int32_t x, int32_t y) { cx = x; cy = y; }
    int32_t getCursorX() const { return cx; }
    void drawChar(int32_t x, int32_t y, unsigned char ch, uint32_t color, uint32_t bgc, uint8_t s) {
        for (int8_t i = 0; i < 6; i++) {
            uint8_t line = (i < 5) ? font[ch * 5 + i] : 0;
            for (int8_t j = 0; j < 8; j++, line >>= 1) {
                uint32_t c = (line & 1) ? color : bgc;
                if ((line & 1) || bgc != color)
                    fillRect(x + i * s, y + j * s, s, s, c);
            }
        }
    }
    size_t write(uint8_t ch) {
        if (ch == '\n') { cy += size * 8; cx = 0; return 1; }
        drawChar(cx, cy, ch, fg, bg, size);
        cx += size * 6;
        return 1;
    }
    void print(const char* s) { while (*s) write((uint8_t)*s++); }
    void print(const String& s) { print(s.c_str()); }
    void print(char c) { write((uint8_t)c); }
    void print(int v) { char b[16]; snprintf(b, sizeof b, "%d", v); print(b); }
    void print(unsigned v) { char b[16]; snprintf(b, sizeof b, "%u", v); print(b); }
    void print(long v) { char b[24]; snprintf(b, sizeof b, "%ld", v); print(b); }
    int16_t textWidth(const char* s) { return (int16_t)(strlen(s) * 6 * size); }
};
