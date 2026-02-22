#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

#include <Arduino.h>
#include <RTClib.h>
#include "types.h"

// ========================================
// Alarm & Mode Management Functions
// ========================================

// Random interval management
void calculateNextRandomInterval();
void initializeRandomInterval();

// Wake configuration
void configureNextWake();

// Trigger functions
void triggerActivation(bool noMode = false);
void checkTriggers();

// ========================================
// Global RTC Object (extern)
// ========================================

extern RTC_DS3231 rtc;

#endif // ALARM_MANAGER_H
