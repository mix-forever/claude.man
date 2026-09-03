// Display simulator: compiles the real ui.cpp / pacman.cpp against a mock
// TFT_eSPI and dumps the 284x76 RGB565 framebuffer. Render to PNG with sim.py.
//
//   g++ -std=c++17 -I tools/sim -I include -o /tmp/sim tools/sim/sim.cpp \
//       tools/sim/glcdfont.cpp src/display/ui.cpp src/display/pacman.cpp
//   /tmp/sim main 0.47 0.82 out.raw        # main screen, 5h/7d utilization
//   /tmp/sim token out.raw | config | connecting | error | ota <pct> | notoken
#include "TFT_eSPI.h"
#include "../../src/display/display.h"
#include "../../src/display/ui.h"
#include "../../src/display/pacman.h"
#include <ctime>
#include <string>

TFT_eSPI tft;
void displayInit() { tft.init(); }
void displaySetBrightness(uint8_t) {}
void displayBacklight(bool) {}

static void dump(const char* path) {
    FILE* f = fopen(path, "wb");
    fwrite(tft.fb.data(), 2, tft.fb.size(), f);
    fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: sim <scene> [args] out.raw\n"); return 1; }
    std::string scene = argv[1];
    const char* out = argv[argc - 1];
    tft.init();
    tft.fillScreen(TFT_BLACK);

    time_t now = time(nullptr);
    RateLimit rl;
    rl.valid = true;
    rl.util5h = (argc > 3) ? atof(argv[2]) : 0.47f;
    rl.util7d = (argc > 4) ? atof(argv[3]) : 0.82f;
    rl.reset5hAt = now + 5580;
    rl.reset7dAt = now + 221000;

    pacmanSetThreshold(0.8f);
    if (scene == "main") {
        pacmanSetFraction(rl.util5h);
        uiShowMain(rl, true, true, 0, "10.4.8.41");
        pacmanDraw();
    } else if (scene == "main-over") {
        pacmanSetFraction(1.0f);
        rl.util5h = 1.0f;
        uiShowMain(rl, true, true, 0, "10.4.8.41");
        pacmanDraw();
    } else if (scene == "main-noalarm") {
        pacmanSetThreshold(0.0f);
        pacmanSetFraction(rl.util5h);
        uiShowMain(rl, true, true, 0, "10.4.8.41");
        pacmanDraw();
    } else if (scene == "tick") {
        // exercise the partial redraw path: several animation frames
        pacmanSetFraction(rl.util5h);
        uiShowMain(rl, true, true, 0, "10.4.8.41");
        pacmanDraw();
        for (int i = 0; i < 5; i++) { delay(130); pacmanTick(); }
    } else if (scene == "main-err") {
        pacmanSetFraction(rl.util5h);
        uiShowMain(rl, true, false, 429, "10.4.8.41");
        pacmanDraw();
    } else if (scene == "main-nodata") {
        RateLimit empty;
        uiShowMain(empty, true, false, 0, "10.4.8.41");
        pacmanDraw();
    } else if (scene == "overlay") {
        pacmanSetFraction(rl.util5h);
        uiShowMain(rl, true, true, 0, "10.4.8.41");
        pacmanDraw();
        uiDrawOverlay("BL 75%", 0xFFE0);
    } else if (scene == "token") {
        uiShowTokenExpired("10.4.8.41");
    } else if (scene == "notoken") {
        uiShowTokenExpired("10.4.8.41", "BRAK TOKENU");
    } else if (scene == "config") {
        uiShowConfigMode("Claude-Monitor-78E8", "192.168.4.1", "K7RM2XQ9");
    } else if (scene == "config-fail") {
        uiShowConfigMode("Claude-Monitor-78E8", "192.168.4.1", "K7RM2XQ9", "BLAD WIFI - SPRAWDZ HASLO");
    } else if (scene == "connecting") {
        uiShowConnecting("DomowaSiec_5G");
    } else if (scene == "error") {
        uiShowError("WiFi 2/3 st:6 DomowaSiec_5G", 17);
    } else if (scene == "ota") {
        int pct = (argc > 3) ? atoi(argv[2]) : 63;
        uiShowOtaProgress(0);
        if (pct > 0) uiShowOtaProgress(pct);
    } else if (scene == "ota-fail") {
        uiShowOtaProgress(-1);
    } else if (scene == "message") {
        uiShowMessage("Token reset...");
    } else {
        fprintf(stderr, "unknown scene %s\n", scene.c_str());
        return 1;
    }
    dump(out);
    return 0;
}
