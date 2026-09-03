#include "buttons.h"
#include "../../include/config.h"

static const uint32_t DEBOUNCE_MS   = 50;
static const uint32_t LONG_PRESS_MS = 1500;

struct BtnState {
    uint8_t  pin;
    bool     lastRaw;
    bool     stable;
    uint32_t debounceAt;
    uint32_t pressAt;
    bool     longFired;
};

static BtnState states[2];

void buttonsInit() {
    states[0] = { PIN_BTN_ALARM,  false, false, 0, 0, false };  // released
    states[1] = { PIN_BTN_BRIGHT, false, false, 0, 0, false };  // released
    pinMode(PIN_BTN_ALARM,  INPUT_PULLUP);
    pinMode(PIN_BTN_BRIGHT, INPUT_PULLUP);
}

bool buttonsIsPressed(Button btn) {
    return digitalRead(states[btn].pin) == LOW;
}

ButtonEvent buttonsGetEvent(Button btn) {
    BtnState& s = states[btn];
    bool raw = digitalRead(s.pin) == LOW;
    uint32_t now = millis();

    if (raw != s.lastRaw) {
        s.lastRaw = raw;
        s.debounceAt = now;
        return EVT_NONE;
    }

    if ((int32_t)(now - s.debounceAt) < (int32_t)DEBOUNCE_MS)
        return EVT_NONE;

    // Stable press
    if (raw && !s.stable) {
        s.stable   = true;
        s.pressAt  = now;
        s.longFired = false;
        return EVT_NONE;
    }

    // Stable release → short press
    if (!raw && s.stable) {
        s.stable = false;
        if (!s.longFired) {
            return EVT_SHORT;
        }
        return EVT_NONE;
    }

    // Still pressed → long press
    if (s.stable && raw && !s.longFired) {
        if ((int32_t)(now - s.pressAt) >= (int32_t)LONG_PRESS_MS) {
            s.longFired = true;
            return EVT_LONG;
        }
    }

    return EVT_NONE;
}
