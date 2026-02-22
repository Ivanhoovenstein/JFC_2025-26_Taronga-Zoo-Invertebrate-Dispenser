#ifndef POWER_MANAGEMENT_H
#define POWER_MANAGEMENT_H

#include <Arduino.h>
#include <esp_sleep.h>
#include "config.h"

// ========================================
// Power Management Functions
// ========================================

void enterDeepSleep();
bool shouldEnterSleep();

#endif // POWER_MANAGEMENT_H
