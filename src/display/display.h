#pragma once

#include <TFT_eSPI.h>

extern TFT_eSPI tft;

void displayInit();
void displayBacklight(bool on);
void displaySetBrightness(uint8_t pct);  // 0..100
