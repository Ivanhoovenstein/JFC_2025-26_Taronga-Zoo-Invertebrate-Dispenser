#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>
#include <vector>

// ========================================
// Data Structures
// ========================================

struct Alarm {
    uint32_t id;
    String time;   // "HH:MM"
    bool active;
};

struct ModeConfig {
    String activeMode;  // "set_times", "regular_interval", "random_interval"
    
    // Regular interval config
    int regIntervalHours;
    int regIntervalMinutes;
    uint32_t regIntervalLastTriggerUnix;  // Unix timestamp (AEST) when last triggered
    
    // Random interval config
    int randIntervalHours;
    int randIntervalMinutes;
    uint32_t randIntervalBlockStartUnix;  // Unix timestamp when next random time should be calculated
    uint32_t randIntervalNextTriggerUnix; // Unix timestamp (AEST) when to trigger
};

struct EventLog {
    uint32_t timestamp;      // Unix timestamp (AEST)
    String type;            // "SUCCESS" or "ERROR"
    String mode;            // "set_times", "regular_interval", "random_interval"
    String message;         // Description of event
};

// ========================================
// Global Variables (extern declarations)
// ========================================

extern std::vector<EventLog> eventHistory;
extern std::vector<Alarm> alarms;
extern ModeConfig modeConfig;

extern int compartment;
extern int maxCompartment;

extern unsigned long apStartTime;
extern bool apModeActive;

extern String currentSSID;

#endif // TYPES_H
