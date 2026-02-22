#include "alarm_manager.h"
#include "storage.h"
#include "servo_control.h"
#include "config.h"

// ========================================
// Random Interval Management
// ========================================

void calculateNextRandomInterval() {
    DateTime now = rtc.now();
    uint32_t currentUnix = now.unixtime();
    uint32_t intervalSeconds = (modeConfig.randIntervalHours * 3600UL + 
                                modeConfig.randIntervalMinutes * 60UL);
    
    // Calculate when the NEXT interval block should start
    uint32_t nextBlockStartUnix = modeConfig.randIntervalBlockStartUnix + intervalSeconds;
    
    // Calculate random offset within the NEW block
    uint32_t randomOffset = random(0, intervalSeconds);
    
    // Set the new block start time and trigger time
    modeConfig.randIntervalBlockStartUnix = nextBlockStartUnix;
    modeConfig.randIntervalNextTriggerUnix = nextBlockStartUnix + randomOffset;
    
    saveModeConfig();
    
    Serial.println("\n=== Random Interval Calculated ===");
    
    DateTime blockStart(nextBlockStartUnix);
    DateTime triggerTime(modeConfig.randIntervalNextTriggerUnix);
    
    Serial.printf("Next interval block starts: %02d:%02d:%02d\n",
                 blockStart.hour(), blockStart.minute(), blockStart.second());
    Serial.printf("Random trigger time: %02d:%02d:%02d\n",
                 triggerTime.hour(), triggerTime.minute(), triggerTime.second());
    Serial.printf("Random offset: %lu seconds (%lu minutes)\n", 
                 randomOffset, randomOffset / 60);
    Serial.println("================================\n");
}

void initializeRandomInterval() {
    DateTime now = rtc.now();
    uint32_t currentUnix = now.unixtime();
    uint32_t intervalSeconds = (modeConfig.randIntervalHours * 3600UL + 
                                modeConfig.randIntervalMinutes * 60UL);
    
    // Start the first interval block NOW
    modeConfig.randIntervalBlockStartUnix = currentUnix;
    
    // Calculate random trigger time within this first block
    uint32_t randomOffset = random(0, intervalSeconds);
    modeConfig.randIntervalNextTriggerUnix = currentUnix + randomOffset;
    
    saveModeConfig();
    
    Serial.println("\n=== Random Interval Initialized ===");
    Serial.printf("First interval block starts: NOW (%lu)\n", currentUnix);
    Serial.printf("Random trigger in %lu seconds (%lu minutes)\n", 
                 randomOffset, randomOffset / 60);
    Serial.println("===================================\n");
}

// ========================================
// Wake Configuration
// ========================================

void configureNextWake() {
    DateTime now = rtc.now();
    DateTime nextWake;
    bool alarmSet = false;
    
    Serial.println("\n=== Configuring Next Wake ===");
    Serial.printf("Current time (AEST): %02d-%02d-%04d %02d:%02d:%02d\n",
                 now.day(), now.month(), now.year(),
                 now.hour(), now.minute(), now.second());
    
    if (modeConfig.activeMode == "set_times") {
        Serial.println("Mode: Set Times");
        
        for (auto &a : alarms) {
            if (a.active) {
                int alarmHour = a.time.substring(0, 2).toInt();
                int alarmMin = a.time.substring(3, 5).toInt();
                
                DateTime alarmToday(now.year(), now.month(), now.day(), 
                                   alarmHour, alarmMin, 0);
                
                if (alarmToday.unixtime() > now.unixtime()) {
                    nextWake = alarmToday;
                    alarmSet = true;
                    Serial.printf("Next alarm today: %s\n", a.time.c_str());
                    break;
                } else if (!alarmSet) {
                    DateTime tomorrow(now.unixtime() + 86400UL);
                    nextWake = DateTime(tomorrow.year(), tomorrow.month(), tomorrow.day(),
                                       alarmHour, alarmMin, 0);
                    alarmSet = true;
                    Serial.printf("Next alarm tomorrow: %s\n", a.time.c_str());
                }
            }
        }
        
        if (!alarmSet) {
            Serial.println("No active alarms found");
        }
    } 
    else if (modeConfig.activeMode == "regular_interval") {
        Serial.println("Mode: Regular Interval");
        
        uint32_t intervalSeconds = (modeConfig.regIntervalHours * 3600UL + 
                                     modeConfig.regIntervalMinutes * 60UL);
        
        if (intervalSeconds > 0) {
            uint32_t currentUnix = now.unixtime();
            
            if (modeConfig.regIntervalLastTriggerUnix == 0) {
                Serial.println("First run - scheduling next trigger from now");
                nextWake = DateTime(currentUnix + intervalSeconds);
                alarmSet = true;
            } else {
                uint32_t nextTriggerUnix = modeConfig.regIntervalLastTriggerUnix + intervalSeconds;
                
                if (currentUnix >= nextTriggerUnix) {
                    Serial.println("Overdue - triggering soon");
                    nextWake = DateTime(currentUnix + 60);
                } else {
                    nextWake = DateTime(nextTriggerUnix);
                    uint32_t remaining = nextTriggerUnix - currentUnix;
                    Serial.printf("Next trigger in %lu seconds (%lu minutes)\n", 
                                 remaining, remaining / 60);
                }
                alarmSet = true;
            }
            
            Serial.printf("Interval: %dh %dm (%lu seconds)\n", 
                         modeConfig.regIntervalHours, 
                         modeConfig.regIntervalMinutes,
                         intervalSeconds);
        } else {
            Serial.println("Invalid interval (0 seconds)");
        }
    }
    else if (modeConfig.activeMode == "random_interval") {
        Serial.println("Mode: Random Interval");
        
        uint32_t currentUnix = now.unixtime();
        
        if (modeConfig.randIntervalNextTriggerUnix == 0 || 
            modeConfig.randIntervalBlockStartUnix == 0) {
            Serial.println("Initializing random interval for first time");
            initializeRandomInterval();
        }
        
        if (modeConfig.randIntervalNextTriggerUnix > currentUnix) {
            nextWake = DateTime(modeConfig.randIntervalNextTriggerUnix);
            alarmSet = true;
            
            uint32_t remaining = modeConfig.randIntervalNextTriggerUnix - currentUnix;
            Serial.printf("Next trigger in %lu seconds (%lu minutes)\n", 
                         remaining, remaining / 60);
            
            DateTime blockStart(modeConfig.randIntervalBlockStartUnix);
            Serial.printf("Current interval block started at: %02d:%02d:%02d\n",
                         blockStart.hour(), blockStart.minute(), blockStart.second());
        } 
        else {
            Serial.println("Trigger time passed - recalculating");
            
            uint32_t intervalSeconds = (modeConfig.randIntervalHours * 3600UL + 
                                        modeConfig.randIntervalMinutes * 60UL);
            
            uint32_t timeSinceBlockStart = currentUnix - modeConfig.randIntervalBlockStartUnix;
            uint32_t blocksPassed = timeSinceBlockStart / intervalSeconds;
            
            modeConfig.randIntervalBlockStartUnix += (blocksPassed + 1) * intervalSeconds;
            
            uint32_t randomOffset = random(0, intervalSeconds);
            modeConfig.randIntervalNextTriggerUnix = modeConfig.randIntervalBlockStartUnix + randomOffset;
            
            saveModeConfig();
            
            nextWake = DateTime(modeConfig.randIntervalNextTriggerUnix);
            alarmSet = true;
            
            Serial.printf("Skipped %lu interval blocks\n", blocksPassed + 1);
            Serial.printf("New trigger time: %02d:%02d:%02d\n",
                         nextWake.hour(), nextWake.minute(), nextWake.second());
        }
    }
    
    if (alarmSet) {
        rtc.disableAlarm(2);
        rtc.clearAlarm(1);
        rtc.clearAlarm(2);
        rtc.writeSqwPinMode(DS3231_OFF);
        
        rtc.setAlarm1(nextWake, DS3231_A1_Hour);
        
        Serial.printf("Next wake scheduled for (AEST): %04d-%02d-%02d %02d:%02d:%02d\n",
                     nextWake.year(), nextWake.month(), nextWake.day(),
                     nextWake.hour(), nextWake.minute(), nextWake.second());
        Serial.printf("Unix timestamp: %lu\n", nextWake.unixtime());
    } else {
        Serial.println("No alarm set - will wake on button press only");
    }
    
    Serial.println("=============================\n");
}

// ========================================
// Trigger Functions
// ========================================

void triggerActivation(bool noMode) {
    Serial.println("========================================");
    Serial.println("TRIGGER EVENT!");
    Serial.printf("Mode: %s\n", modeConfig.activeMode.c_str());
    
    DateTime now = rtc.now();
    Serial.printf("Time: %02d:%02d:%02d\n", now.hour(), now.minute(), now.second());
    
    advanceCompartment();

    bool success = true;
    String errorMessage = "";
    String warning = "";
    
    if (!LittleFS.begin()) {
        warning = "LittleFS not accessible - compartment position may not persist";
        Serial.println("WARNING: " + warning);
        logEvent("WARNING", modeConfig.activeMode, warning);
    }

    if (!rtc.begin(&Wire)) {
        warning = "RTC communication error - clock may have lost power";
        Serial.println("ERROR: " + warning);
        logEvent("WARNING", modeConfig.activeMode, warning);
    }

    if (rtc.lostPower()) {
        warning = "RTC lost power - time may be incorrect, battery may need replacement";
        Serial.println("WARNING: " + warning);
        logEvent("WARNING", modeConfig.activeMode, warning);
    }

    if (digitalRead(SERVO_TRANSISTOR_PIN) != HIGH) {
        success = false;
        errorMessage = "Servo power transistor failed to activate";
        Serial.println("ERROR: " + errorMessage);
        logEvent("ERROR", modeConfig.activeMode, errorMessage);
        Serial.println("========================================");
        return;
    }

    String compartmentActivationStr = String(compartment + 1);
    if (compartment == 0) compartmentActivationStr = "6";
    
    String successMessage = "Activation completed successfully (Chamber " + compartmentActivationStr + ")";

    if (success) {
        if (noMode) {
            logEvent("SUCCESS", "Manual Activation", successMessage);
        } else {
            logEvent("SUCCESS", modeConfig.activeMode, successMessage);
        }
    } else {
        logEvent("ERROR", modeConfig.activeMode, errorMessage);
    }
    
    Serial.println("========================================");
}

void checkTriggers() {
    static unsigned long lastCheck = 0;
    unsigned long now = millis();
    
    if (now - lastCheck < TRIGGER_CHECK_INTERVAL) {
        return;
    }
    lastCheck = now;

    DateTime rtcTime = rtc.now();
    uint32_t currentUnix = rtcTime.unixtime();

    // MODE 1: Set Times
    if (modeConfig.activeMode == "set_times") {
        char current[6];
        snprintf(current, sizeof(current), "%02d:%02d", rtcTime.hour(), rtcTime.minute());

        for (auto &a : alarms) {
            if (a.active && a.time == current && rtcTime.second() == 0) {
                Serial.printf("SET TIMES: Alarm triggered at %s!\n", a.time.c_str());
                triggerActivation();
                break;
            }
        }
    }
    
    // MODE 2: Regular Interval
    else if (modeConfig.activeMode == "regular_interval") {
        uint32_t intervalSeconds = (modeConfig.regIntervalHours * 3600UL + 
                                     modeConfig.regIntervalMinutes * 60UL);
        
        if (intervalSeconds > 0) {
            if (modeConfig.regIntervalLastTriggerUnix == 0) {
                modeConfig.regIntervalLastTriggerUnix = currentUnix;
                saveModeConfig();
                Serial.println("Regular interval initialized");
            }
            
            uint32_t nextTriggerUnix = modeConfig.regIntervalLastTriggerUnix + intervalSeconds;
            
            if (currentUnix >= nextTriggerUnix) {
                Serial.printf("REGULAR INTERVAL: Triggered after %dh %dm\n", 
                             modeConfig.regIntervalHours, modeConfig.regIntervalMinutes);
                
                triggerActivation();
                
                modeConfig.regIntervalLastTriggerUnix = currentUnix;
                saveModeConfig();
            }
        }
    }
    
    // MODE 3: Random Interval
    else if (modeConfig.activeMode == "random_interval") {
        if (modeConfig.randIntervalNextTriggerUnix == 0 || 
            modeConfig.randIntervalBlockStartUnix == 0) {
            initializeRandomInterval();
            return;
        }
        
        if (currentUnix >= modeConfig.randIntervalNextTriggerUnix) {
            Serial.printf("RANDOM INTERVAL: Triggered at random time within %dh %dm window\n",
                         modeConfig.randIntervalHours, modeConfig.randIntervalMinutes);
            
            triggerActivation();
            
            calculateNextRandomInterval();
        }
    }
}
