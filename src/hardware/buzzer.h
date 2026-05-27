#pragma once
#include <Arduino.h>

enum AlarmLevel { ALARM_NONE = 0, ALARM_LOW, ALARM_HIGH, ALARM_CRITICAL };

void buzzerInit();
void buzzerMute(bool mute);
bool buzzerIsMuted();

// Blocking (short — ok for boot / test)
void buzzerPlayStart();
void buzzerTestBeep();

// Non-blocking alarm — call buzzerAlarmTick() every loop()
void buzzerSetAlarmLevel(AlarmLevel lvl);
void buzzerAlarmTick();
