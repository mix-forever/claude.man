#include "display.h"
#include "../../include/config.h"

TFT_eSPI tft;

void displayInit() {
    pinMode(PIN_TFT_BL, OUTPUT);
    ledcSetup(PWM_CH_BL, PWM_FREQ_BL, PWM_RES_BITS);
    ledcAttachPin(PIN_TFT_BL, PWM_CH_BL);
    displayBacklight(true);
    tft.init();
    tft.setRotation(DISPLAY_ROTATION);
    tft.fillScreen(TFT_BLACK);
}

void displaySetBrightness(uint8_t pct) {
    if (pct > 100) pct = 100;
    uint32_t duty = (pct * 255) / 100;
    ledcWrite(PWM_CH_BL, duty);
}

void displayBacklight(bool on) {
    displaySetBrightness(on ? 100 : 0);
}
