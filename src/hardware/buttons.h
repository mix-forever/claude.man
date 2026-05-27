#pragma once
#include <Arduino.h>

enum Button      { BTN_ALARM = 0, BTN_BRIGHT = 1 };
enum ButtonEvent { EVT_NONE = 0, EVT_SHORT, EVT_LONG };

void buttonsInit();
bool buttonsIsPressed(Button btn);
ButtonEvent buttonsGetEvent(Button btn);
