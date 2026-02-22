#include "storage.h"
#include "servo_control.h"
#include "alarm_manager.h"
#include <algorithm>

// ========================================
// Alarm Storage Functions
// ========================================

String alarmsToJson() {
    DynamicJsonDocument doc(JSON_BUFFER_LARGE);
    JsonArray arr = doc.to<JsonArray>();

    for (auto &a : alarms) {
        JsonObject o = arr.createNestedObject();
        o["id"] = a.id;
        o["time"] = a.time;
        o["active"] = a.active;
    }

    String out;
    serializeJson(arr, out);
    return out;
}

void saveAlarms() {
    // Sort alarms by time before saving
    std::sort(alarms.begin(), alarms.end(), [](const Alarm &a, const Alarm &b) {
        return a.time < b.time;
    });

    File f = LittleFS.open(FILE_ALARMS, "w");
    if (!f) {
        Serial.println("Failed to open alarms.json for writing");
        logEvent("ERROR", "System", "Error opening set-times config file (alarms.json)");
        return;
    }
    String jsonStr = alarmsToJson();
    f.print(jsonStr);
    f.close();
    Serial.println("Saved alarms: " + jsonStr);
}

void loadAlarms() {
    if (!LittleFS.exists(FILE_ALARMS)) {
        Serial.println("alarms.json not found, creating new file");
        alarms.clear();
        saveAlarms();
        return;
    }

    File f = LittleFS.open(FILE_ALARMS, "r");
    if (!f) {
        Serial.println("Failed to open alarms.json for reading");
        logEvent("ERROR", "System", "Error opening mode set-times file for reading (alarms.json)");
        return;
    }
    
    String json = f.readString();
    f.close();
    
    Serial.println("Loaded JSON: " + json);
    
    json.trim();
    if (json.length() == 0) {
        Serial.println("Empty JSON file, initializing with empty array");
        alarms.clear();
        saveAlarms();
        return;
    }

    DynamicJsonDocument doc(JSON_BUFFER_LARGE);
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.print("Error parsing alarms.json: ");
        Serial.println(err.c_str());
        Serial.println("JSON content was: " + json);
        alarms.clear();
        saveAlarms();
        return;
    }

    alarms.clear();
    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject o : arr) {
        Alarm a;
        a.id = o["id"].as<uint32_t>();
        a.time = o["time"].as<String>();
        a.active = o["active"].as<bool>();
        alarms.push_back(a);
    }

    std::sort(alarms.begin(), alarms.end(), [](const Alarm &a, const Alarm &b) {
        return a.time < b.time;
    });

    Serial.printf("Loaded %d alarms\n", alarms.size());
}

// ========================================
// Mode Configuration Storage
// ========================================

void saveModeConfig() {
    File f = LittleFS.open(FILE_MODE, "w");
    if (!f) {
        Serial.println("Failed to open mode.json for writing");
        return;
    }
    
    DynamicJsonDocument doc(JSON_BUFFER_MEDIUM);
    doc["activeMode"] = modeConfig.activeMode;
    doc["regIntervalHours"] = modeConfig.regIntervalHours;
    doc["regIntervalMinutes"] = modeConfig.regIntervalMinutes;
    doc["regIntervalLastTriggerUnix"] = modeConfig.regIntervalLastTriggerUnix;
    doc["randIntervalHours"] = modeConfig.randIntervalHours;
    doc["randIntervalMinutes"] = modeConfig.randIntervalMinutes;
    doc["randIntervalBlockStartUnix"] = modeConfig.randIntervalBlockStartUnix;
    doc["randIntervalNextTriggerUnix"] = modeConfig.randIntervalNextTriggerUnix;
    
    String jsonStr;
    serializeJson(doc, jsonStr);
    f.print(jsonStr);
    f.close();
    
    Serial.println("Saved mode config: " + jsonStr);
}

void loadModeConfig() {
    if (!LittleFS.exists(FILE_MODE)) {
        Serial.println("mode.json not found, creating default");
        modeConfig.activeMode = "set_times";
        modeConfig.regIntervalHours = 0;
        modeConfig.regIntervalMinutes = 30;
        modeConfig.regIntervalLastTriggerUnix = 0;
        modeConfig.randIntervalHours = 1;
        modeConfig.randIntervalMinutes = 0;
        modeConfig.randIntervalBlockStartUnix = 0;
        modeConfig.randIntervalNextTriggerUnix = 0;
        saveModeConfig();
        return;
    }
    
    File f = LittleFS.open(FILE_MODE, "r");
    if (!f) {
        Serial.println("Failed to open mode.json");
        return;
    }
    
    String json = f.readString();
    f.close();
    
    DynamicJsonDocument doc(JSON_BUFFER_MEDIUM);
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.print("Error parsing mode.json: ");
        Serial.println(err.c_str());
        return;
    }
    
    modeConfig.activeMode = doc["activeMode"].as<String>();
    modeConfig.regIntervalHours = doc["regIntervalHours"];
    modeConfig.regIntervalMinutes = doc["regIntervalMinutes"];
    modeConfig.regIntervalLastTriggerUnix = doc["regIntervalLastTriggerUnix"];
    modeConfig.randIntervalHours = doc["randIntervalHours"];
    modeConfig.randIntervalMinutes = doc["randIntervalMinutes"];
    modeConfig.randIntervalBlockStartUnix = doc["randIntervalBlockStartUnix"];
    modeConfig.randIntervalNextTriggerUnix = doc["randIntervalNextTriggerUnix"];
    
    Serial.println("Loaded mode config: " + json);
}

// ========================================
// Servo Position Storage
// ========================================

void saveCompartmentPosition() {
    File f = LittleFS.open(FILE_SERVO, "w");
    if (!f) {
        Serial.println("Failed to open servo.json for writing");
        logEvent("ERROR", "system", "Failed to save servo position in servo config (servo.json)");
        return;
    }
    
    DynamicJsonDocument doc(128);
    doc["compartment"] = compartment;
    doc["angle"] = compartment * SERVO_ANGLE_STEP;
    
    String jsonStr;
    serializeJson(doc, jsonStr);
    f.print(jsonStr);
    f.close();
    
    Serial.printf("Saved servo position: compartment=%d, angle=%d\n", 
                  compartment, compartment * SERVO_ANGLE_STEP);
}

void loadCompartmentPosition() {
    if (!LittleFS.exists(FILE_SERVO)) {
        Serial.println("servo.json not found, starting at compartment 0");
        logEvent("ERROR", "System", "Error opening servo config file (servo.json) - File Not Found");
        compartment = 0;
        saveCompartmentPosition();
        return;
    }
    
    File f = LittleFS.open(FILE_SERVO, "r");
    if (!f) {
        Serial.println("Failed to open servo.json for reading");
        logEvent("ERROR", "System", "Error opening servo config file for reading (servo.json)");
        compartment = 0;
        return;
    }
    
    String json = f.readString();
    f.close();
    
    DynamicJsonDocument doc(128);
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.print("Error parsing servo.json: ");
        Serial.println(err.c_str());
        compartment = 0;
        return;
    }
    
    compartment = doc["compartment"];
    int savedAngle = doc["angle"];
    
    Serial.printf("Loaded servo position: compartment=%d, angle=%d\n", 
                  compartment, savedAngle);
}

// ========================================
// WiFi Settings Storage
// ========================================

void loadWiFiSettings() {
    if (!LittleFS.exists(FILE_WIFI)) {
        Serial.println("wifi.json not found, creating default");
        File f = LittleFS.open(FILE_WIFI, "w");
        if (f) {
            f.print("{\"ssid\":\"" DEFAULT_SSID "\"}");
            f.close();
        }
        return;
    }
    
    File f = LittleFS.open(FILE_WIFI, "r");
    if (!f) {
        Serial.println("Failed to open wifi.json");
        logEvent("ERROR", "System", "Error opening wifi config (wifi.json)");
        return;
    }
    
    String json = f.readString();
    f.close();
    
    DynamicJsonDocument doc(JSON_BUFFER_SMALL);
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.print("Error parsing wifi.json: ");
        Serial.println(err.c_str());
        logEvent("ERROR", "System", "Error reading wifi config from file (wifi.json)");
        return;
    }
    
    currentSSID = doc["ssid"].as<String>();
    
    // Validate SSID
    if (currentSSID.length() == 0 || currentSSID.length() > 32) {
        Serial.println("Invalid SSID length, using default");
        currentSSID = DEFAULT_SSID;
    }
    
    Serial.println("Loaded WiFi settings:");
    Serial.println("  SSID: " + currentSSID);
}

void saveWiFiSettings(String ssid) {
    File f = LittleFS.open(FILE_WIFI, "w");
    if (!f) {
        Serial.println("Failed to open wifi.json for writing");
        logEvent("ERROR", "System", "Error opening wifi config for writing (wifi.json)");
        return;
    }
    
    DynamicJsonDocument doc(JSON_BUFFER_SMALL);
    doc["ssid"] = ssid;
    
    String jsonStr;
    serializeJson(doc, jsonStr);
    f.print(jsonStr);
    f.close();
    
    Serial.println("Saved WiFi settings: " + jsonStr);
}

// ========================================
// Settings Storage
// ========================================

void initSettings() {
    if (!LittleFS.exists(FILE_SETTINGS)) {
        Serial.println("settings.json not found, creating default");
        File f = LittleFS.open(FILE_SETTINGS, "w");
        if (f) {
            f.print("{\"timeFormat\":\"12\",\"theme\":\"light\"}");
            f.close();
        }
    }
}

// ========================================
// Event Logging
// ========================================

void logEvent(String type, String mode, String message) {
    extern RTC_DS3231 rtc;
    DateTime now = rtc.now();
    uint32_t currentUnix = now.unixtime();
    
    EventLog event;
    event.timestamp = currentUnix;
    event.type = type;
    event.mode = mode;
    event.message = message;
    
    // Add to in-memory history
    eventHistory.push_back(event);
    
    // Keep only recent events in memory
    if (eventHistory.size() > MAX_EVENTS_IN_MEMORY) {
        eventHistory.erase(eventHistory.begin());
    }
    
    // Save to file
    saveEventToFile(event);
    
    // Format and print to serial
    char timeStr[20];
    snprintf(timeStr, sizeof(timeStr), "%02d-%02d-%04d %02d:%02d:%02d",
             now.day(), now.month(), now.year(),
             now.hour(), now.minute(), now.second());
    
    Serial.printf("[%s] [%s] [%s] %s\n", 
                 timeStr, type.c_str(), mode.c_str(), message.c_str());
}

void saveEventToFile(const EventLog &event) {
    File f = LittleFS.open(FILE_EVENTS, "a");
    if (!f) {
        Serial.println("Failed to open events.log for writing");
        return;
    }
    
    f.printf("%lu,%s,%s,%s\n", 
             event.timestamp, 
             event.type.c_str(), 
             event.mode.c_str(), 
             event.message.c_str());
    
    f.close();
}

void loadEventsFromFile() {
    extern RTC_DS3231 rtc;
    eventHistory.clear();
    
    if (!LittleFS.exists(FILE_EVENTS)) {
        Serial.println("events.log not found");
        return;
    }
    
    File f = LittleFS.open(FILE_EVENTS, "r");
    if (!f) {
        Serial.println("Failed to open events.log for reading");
        return;
    }
    
    DateTime now = rtc.now();
    uint32_t currentUnix = now.unixtime();
    uint32_t cutoffTime = currentUnix - EVENT_RETENTION_SECONDS;
    
    std::vector<EventLog> tempEvents;
    
    // Read all events
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        
        if (line.length() == 0) continue;
        
        // Parse CSV: timestamp,type,mode,message
        int firstComma = line.indexOf(',');
        int secondComma = line.indexOf(',', firstComma + 1);
        int thirdComma = line.indexOf(',', secondComma + 1);
        
        if (firstComma == -1 || secondComma == -1 || thirdComma == -1) {
            Serial.println("Malformed log line: " + line);
            continue;
        }
        
        EventLog event;
        event.timestamp = line.substring(0, firstComma).toInt();
        event.type = line.substring(firstComma + 1, secondComma);
        event.mode = line.substring(secondComma + 1, thirdComma);
        event.message = line.substring(thirdComma + 1);
        
        // Only keep events from last 24 hours
        if (event.timestamp >= cutoffTime) {
            tempEvents.push_back(event);
        }
    }
    
    f.close();
    
    // Rewrite file with only recent events
    f = LittleFS.open(FILE_EVENTS, "w");
    if (f) {
        for (const auto &event : tempEvents) {
            f.printf("%lu,%s,%s,%s\n", 
                     event.timestamp, 
                     event.type.c_str(), 
                     event.mode.c_str(), 
                     event.message.c_str());
            
            if (eventHistory.size() < MAX_EVENTS_IN_MEMORY) {
                eventHistory.push_back(event);
            }
        }
        f.close();
    }
    
    Serial.printf("Loaded %d events from log file\n", eventHistory.size());
}

String eventsToJson() {
    extern RTC_DS3231 rtc;
    DateTime now = rtc.now();
    uint32_t currentUnix = now.unixtime();
    uint32_t cutoffTime = currentUnix - EVENT_RETENTION_SECONDS;
    
    DynamicJsonDocument doc(JSON_BUFFER_XLARGE);
    JsonArray arr = doc.to<JsonArray>();
    
    // Add events from newest to oldest
    for (int i = eventHistory.size() - 1; i >= 0; i--) {
        const EventLog &event = eventHistory[i];
        
        if (event.timestamp >= cutoffTime) {
            JsonObject o = arr.createNestedObject();
            o["timestamp"] = event.timestamp;
            o["type"] = event.type;
            o["mode"] = event.mode;
            o["message"] = event.message;
            
            DateTime dt(event.timestamp);
            char timeStr[20];
            snprintf(timeStr, sizeof(timeStr), "%02d-%02d-%04d %02d:%02d:%02d",
                     dt.day(), dt.month(), dt.year(),
                     dt.hour(), dt.minute(), dt.second());
            o["timeStr"] = timeStr;
        }
    }
    
    String out;
    serializeJson(arr, out);
    return out;
}
