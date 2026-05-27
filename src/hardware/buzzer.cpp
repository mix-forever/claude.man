#include "buzzer.h"
#include "../../include/config.h"

static bool        _muted      = false;
static AlarmLevel  _alarmLvl   = ALARM_NONE;
static bool        _toneOn     = false;
static uint32_t    _nextToggle = 0;

// ─── Pattern tables (freq Hz, tone ms, pause ms) ────────────────────────────
static const uint16_t FREQ_LOW[]    = { 800,  600 };
static const uint16_t FREQ_HIGH[]   = { 2000, 2000, 2000 };
static const uint16_t FREQ_CRIT[]   = { 2000, 2000, 2000 };
static const uint16_t TONE_DUR[]    = { 100, 80, 200 };
static const uint16_t PAUSE_LVL[]   = { 3000, 1500, 400 };
static       uint8_t  _seqIdx       = 0;

void buzzerInit() {
    pinMode(PIN_BUZZER, OUTPUT);
    ledcSetup(PWM_CH_BUZZER, PWM_FREQ_BUZZ, PWM_RES_BITS);
    ledcAttachPin(PIN_BUZZER, PWM_CH_BUZZER);
    ledcWrite(PWM_CH_BUZZER, 0);
}

void buzzerMute(bool mute) { _muted = mute; }
bool buzzerIsMuted()       { return _muted; }

// ─── Helpers ─────────────────────────────────────────────────────────────────
static void toneOn(uint16_t freq) {
    if (_muted) return;
    ledcWriteTone(PWM_CH_BUZZER, freq);
    ledcWrite(PWM_CH_BUZZER, 128);
}
static void toneOff() {
    ledcWrite(PWM_CH_BUZZER, 0);
    ledcWriteTone(PWM_CH_BUZZER, 0);
}

static void playTone(uint16_t freq, uint32_t ms) {
    toneOn(freq);
    delay(ms);
    toneOff();
}

// ─── Boot melody (blocking, ok in setup) ─────────────────────────────────────
void buzzerPlayStart() {
    if (_muted) return;
    playTone(1047, 150);  // C6
    delay(40);
    playTone(1319, 150);  // E6
    delay(40);
    playTone(1568, 200);  // G6
    delay(100);
}

// ─── Test beep (blocking, user invoked) ──────────────────────────────────────
void buzzerTestBeep() {
    bool wasMuted = _muted;
    _muted = false;
    playTone(1500, 150);
    _muted = wasMuted;
}

// ─── Non-blocking alarm ──────────────────────────────────────────────────────
void buzzerSetAlarmLevel(AlarmLevel lvl) {
    if (lvl != _alarmLvl) {
        _alarmLvl = lvl;
        _seqIdx = 0;
        _toneOn = false;
        _nextToggle = millis();
        if (lvl == ALARM_NONE) toneOff();
    }
}

void buzzerAlarmTick() {
    if (_alarmLvl == ALARM_NONE) return;
    if (_muted) {
        if (_toneOn) { toneOff(); _toneOn = false; }
        return;
    }

    uint32_t now = millis();
    if ((int32_t)(now - _nextToggle) < 0) return;

    const uint16_t* freqs;
    uint8_t seqLen;
    uint16_t dur, pause;

    switch (_alarmLvl) {
        case ALARM_LOW:
            freqs = FREQ_LOW;  seqLen = 2;
            dur = TONE_DUR[0]; pause = PAUSE_LVL[0];
            break;
        case ALARM_HIGH:
            freqs = FREQ_HIGH; seqLen = 3;
            dur = TONE_DUR[1]; pause = PAUSE_LVL[1];
            break;
        case ALARM_CRITICAL:
            freqs = FREQ_CRIT; seqLen = 3;
            dur = TONE_DUR[2]; pause = PAUSE_LVL[2];
            break;
        default:
            toneOff(); return;
    }

    if (_toneOn) {
        toneOff();
        _toneOn = false;
        _nextToggle = now + pause;
        _seqIdx = (_seqIdx + 1) % seqLen;
    } else {
        toneOn(freqs[_seqIdx]);
        _toneOn = true;
        _nextToggle = now + dur;
    }
}
