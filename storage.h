#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "types.h"

// ========================================
// Storage Functions
// ========================================

// Alarm storage
void saveAlarms();
void loadAlarms();
String alarmsToJson();

// Mode configuration storage
void saveModeConfig();
void loadModeConfig();

// Servo position storage
void saveCompartmentPosition();
void loadCompartmentPosition();

// WiFi settings storage
void loadWiFiSettings();
void saveWiFiSettings(String ssid);

// Settings storage
void initSettings();

// Event logging
void logEvent(String type, String mode, String message);
void saveEventToFile(const EventLog &event);
void loadEventsFromFile();
String eventsToJson();

#endif // STORAGE_H
