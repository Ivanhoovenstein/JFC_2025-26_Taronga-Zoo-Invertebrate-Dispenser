#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <RTClib.h>
#include <algorithm>
#include <esp_sleep.h>
#include <ESP32Servo.h>
#include <Adafruit_INA219.h>

#define I2C_SDA_PIN 33
#define I2C_SCL_PIN 32
#define RTC_ALARM_PIN 25    // DS3231 SQW pin connected 
#define BUTTON_PIN 34       // Button for manual wake
#define LED_PIN 14
#define SERVO_TRANSISTOR_PIN 13

// Servo global variables
Servo myServo;

const int servoPin = 12;
int minPulse = 500;
int maxPulse = 2500;
const float MECH_RANGE = 360;
int compartment = 0;
int maxCompartment = 6; // This is our design for now.

// AP timeout settings
#define AP_TIMEOUT_MS 900000UL  // 15 minutes in milliseconds
unsigned long apStartTime = 0;
bool apModeActive = false;

// Battery Sensor
Adafruit_INA219 ina219;

// Real Time Clock
RTC_DS3231 rtc;
WebServer server(80);
DNSServer dnsServer;

const byte DNS_PORT = 53;
String currentSSID = "Taronga Zoo Curlew Feeder";
String currentPassword = "12345678";  // change this!

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
    uint32_t randIntervalBlockStartUnix; // Unix timestamp when next random time should be calculated
    uint32_t randIntervalNextTriggerUnix; // Unix timestamp (AEST) when to trigger
};

// Event logging structure
struct EventLog {
    uint32_t timestamp;      // Unix timestamp (AEST)
    String type;            // "SUCCESS" or "ERROR"
    String mode;            // "set_times", "regular_interval", "random_interval"
    String message;         // Description of event
};

std::vector<EventLog> eventHistory;
std::vector<Alarm> alarms;
ModeConfig modeConfig;

const int MAX_EVENTS_IN_MEMORY = 100;  // Keep last 100 events in memory
const uint32_t EVENT_RETENTION_SECONDS = 86400;  // 24 hours


void triggerActivation(bool noMode = false);

// ----------------------------------------
// Calculate next random interval block
// ----------------------------------------
void calculateNextRandomInterval() {
    DateTime now = rtc.now();
    uint32_t currentUnix = now.unixtime();
    uint32_t intervalSeconds = (modeConfig.randIntervalHours * 3600UL + 
                                modeConfig.randIntervalMinutes * 60UL);
    
    // Calculate when the NEXT interval block should start
    // If we just triggered, next block starts one full interval from the block start
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

// ----------------------------------------
// Initialize random interval (first time setup)
// ----------------------------------------
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

// ----------------------------------------
// Calculate next wake time and configure RTC alarm
// ----------------------------------------
void configureNextWake() {
    DateTime now = rtc.now(); // AEST time
    DateTime nextWake;
    bool alarmSet = false;
    
    Serial.println("\n=== Configuring Next Wake ===");
    Serial.printf("Current time (AEST): %02d-%02d-%04d %02d:%02d:%02d\n",
                 now.day(), now.month(), now.year(),
                 now.hour(), now.minute(), now.second());
    
    if (modeConfig.activeMode == "set_times") {
        Serial.println("Mode: Set Times");
        
        // Find next active alarm
        for (auto &a : alarms) {
            if (a.active) {
                int alarmHour = a.time.substring(0, 2).toInt();
                int alarmMin = a.time.substring(3, 5).toInt();
                
                // Create DateTime for alarm today (AEST)
                DateTime alarmToday(now.year(), now.month(), now.day(), 
                                   alarmHour, alarmMin, 0);
                
                // Check if alarm is in the future today
                if (alarmToday.unixtime() > now.unixtime()) {
                    nextWake = alarmToday;
                    alarmSet = true;
                    Serial.printf("Next alarm today: %s\n", a.time.c_str());
                    break;
                } else if (!alarmSet) {
                    // First alarm tomorrow (add 86400 seconds = 1 day)
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
            
            // If first run or no previous trigger
            if (modeConfig.regIntervalLastTriggerUnix == 0) {
                Serial.println("First run - scheduling next trigger from now");
                nextWake = DateTime(currentUnix + intervalSeconds);
                alarmSet = true;
            } else {
                // Calculate next trigger based on last trigger
                uint32_t nextTriggerUnix = modeConfig.regIntervalLastTriggerUnix + intervalSeconds;
                
                // If we're past the next trigger time (overdue)
                if (currentUnix >= nextTriggerUnix) {
                    Serial.println("Overdue - triggering soon");
                    // Trigger 1 minute from now
                    nextWake = DateTime(currentUnix + 60);
                } else {
                    // Normal case - schedule for calculated time
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
        
        // Check if we need to initialize
        if (modeConfig.randIntervalNextTriggerUnix == 0 || 
            modeConfig.randIntervalBlockStartUnix == 0) {
            Serial.println("Initializing random interval for first time");
            initializeRandomInterval();
        }
        
        // Check if the trigger time is in the future
        if (modeConfig.randIntervalNextTriggerUnix > currentUnix) {
            // Normal case - use the calculated trigger time
            nextWake = DateTime(modeConfig.randIntervalNextTriggerUnix);
            alarmSet = true;
            
            uint32_t remaining = modeConfig.randIntervalNextTriggerUnix - currentUnix;
            Serial.printf("Next trigger in %lu seconds (%lu minutes)\n", 
                         remaining, remaining / 60);
            
            // Show which interval block we're in
            DateTime blockStart(modeConfig.randIntervalBlockStartUnix);
            Serial.printf("Current interval block started at: %02d:%02d:%02d\n",
                         blockStart.hour(), blockStart.minute(), blockStart.second());
        } 
        else {
            // Overdue or past - recalculate from current block
            Serial.println("Trigger time passed - recalculating");
            
            uint32_t intervalSeconds = (modeConfig.randIntervalHours * 3600UL + 
                                        modeConfig.randIntervalMinutes * 60UL);
            
            // Find which interval block we should be in
            uint32_t timeSinceBlockStart = currentUnix - modeConfig.randIntervalBlockStartUnix;
            uint32_t blocksPassed = timeSinceBlockStart / intervalSeconds;
            
            // Move to the next block after the ones that passed
            modeConfig.randIntervalBlockStartUnix += (blocksPassed + 1) * intervalSeconds;
            
            // Calculate new random time in this new block
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
        // Set DS3231 alarm
        rtc.disableAlarm(2);
        rtc.clearAlarm(1);
        rtc.clearAlarm(2);
        rtc.writeSqwPinMode(DS3231_OFF);
        
        // Set Alarm 1 to trigger at specific date/time (in AEST)
        rtc.setAlarm1(nextWake, DS3231_A1_Hour); // Match hours and minutes
        
        Serial.printf("Next wake scheduled for (AEST): %04d-%02d-%02d %02d:%02d:%02d\n",
                     nextWake.year(), nextWake.month(), nextWake.day(),
                     nextWake.hour(), nextWake.minute(), nextWake.second());
        Serial.printf("Unix timestamp: %lu\n", nextWake.unixtime());
    } else {
        Serial.println("No alarm set - will wake on button press only");
    }
    
    Serial.println("=============================\n");
}

// ----------------------------------------
// Enter deep sleep mode
// ----------------------------------------
void enterDeepSleep() {
    Serial.println("\n========================================");
    Serial.println("Preparing to enter deep sleep...");
    
    // Save all data before sleeping
    saveAlarms();
    saveModeConfig();
    saveCompartmentPosition();
    
    // ----------------------------------------
    // 1. DETACH SERVO (big power consumer)
    // ----------------------------------------
    if (myServo.attached()) {
        myServo.detach();
        Serial.println("Servo detached");
    }
    
    // ----------------------------------------
    // 2. TURN OFF WiFi
    // ----------------------------------------
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);
    Serial.println("WiFi turned off");
    
    // ----------------------------------------
    // 3. TURN OFF I2C / Wire
    // ----------------------------------------
    Wire.end();
    Serial.println("I2C stopped");
    
    // ----------------------------------------
    // 4. SET ALL UNUSED GPIOs TO LOW
    // ----------------------------------------
    // List ALL GPIO pins your ESP32 has that are NOT used as wake sources
    // This is critical - floating pins can consume current
    const int unusedPins[] = {
        0, 2, 4, 5, 12, 13, 15,
        16, 17, 18, 19, 20, 21,
        22, 23
        // EXCLUDE: 25 (RTC_ALARM_PIN), 32 (SCL), 33 (SDA), 34 (BUTTON_PIN)
    };

    for (int pin : unusedPins) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }
    Serial.println("Unused GPIOs set low");

    // turn off servo transistor
    digitalWrite(SERVO_TRANSISTOR_PIN, LOW);
    
    // ----------------------------------------
    // 5. CONFIGURE WAKE SOURCES
    // ----------------------------------------
    esp_sleep_enable_ext0_wakeup((gpio_num_t)RTC_ALARM_PIN, 0);  // RTC alarm (SQW goes LOW)
    esp_sleep_enable_ext1_wakeup(1ULL << BUTTON_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);  // Button
    
    // ----------------------------------------
    // 6. DISABLE EVERYTHING ELSE
    // ----------------------------------------
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    
    Serial.println("\nWake sources configured:");
    Serial.printf("  - RTC Alarm on GPIO %d (active LOW)\n", RTC_ALARM_PIN);
    Serial.printf("  - Button on GPIO %d (active HIGH)\n", BUTTON_PIN);
    Serial.println("Entering deep sleep NOW...");
    Serial.println("========================================\n");
    
    delay(100);  // Give serial time to flush
    
    // Enter deep sleep
    esp_deep_sleep_start();
}

// ----------------------------------------
// Check if we should enter sleep mode
// ----------------------------------------
bool shouldEnterSleep() {
    // Don't sleep if AP mode is active and timeout hasn't expired
    if (apModeActive) {
        unsigned long elapsed = millis() - apStartTime;
        if (elapsed < AP_TIMEOUT_MS) {
            return false; // Stay awake
        } else {
            Serial.println("AP mode timeout reached");
            return true; // Timeout expired, go to sleep
        }
    }
    
    return true; // Not in AP mode, can sleep
}


// ----------------------------------------
// Add CORS headers to all responses
// ----------------------------------------
void setCORSHeaders() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, PATCH, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// ----------------------------------------
// Utility: Convert alarms to JSON string
// ----------------------------------------
String alarmsToJson() {
    DynamicJsonDocument doc(4096);
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

// ----------------------------------------
// Save compartment position to LittleFS
// ----------------------------------------
void saveCompartmentPosition() {
    File f = LittleFS.open("/servo.json", "w");
    if (!f) {
        Serial.println("Failed to open servo.json for writing");
        logEvent("ERROR", "system", "Failed to save servo position in servo config (servo.json)");
        return;
    }
    
    DynamicJsonDocument doc(128);
    doc["compartment"] = compartment;
    doc["angle"] = compartment * 60;  // Store current angle too
    
    String jsonStr;
    serializeJson(doc, jsonStr);
    f.print(jsonStr);
    f.close();
    
    Serial.printf("Saved servo position: compartment=%d, angle=%d\n", 
                  compartment, compartment * 60);
}

// ----------------------------------------
// Load compartment position from LittleFS
// ----------------------------------------
void loadCompartmentPosition() {
    if (!LittleFS.exists("/servo.json")) {
        Serial.println("servo.json not found, starting at compartment 0");
        logEvent("ERROR", "System", "Error opening servo config file (servo.json) - File Not Found");

        compartment = 0;
        saveCompartmentPosition();
        return;
    }
    
    File f = LittleFS.open("/servo.json", "r");
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

// ----------------------------------------
// Load WiFi settings from LittleFS
// ----------------------------------------
void loadWiFiSettings() {
    if (!LittleFS.exists("/wifi.json")) {
        Serial.println("wifi.json not found, creating default");
        File f = LittleFS.open("/wifi.json", "w");
        if (f) {
            // f.print("{\"ssid\":\"Taronga Zoo Curlew Feeder\",\"password\":\"12345678\"}");
            f.print("{\"ssid\":\"Taronga Zoo Curlew Feeder\"}");
            f.close();
        }
        return;
    }
    
    File f = LittleFS.open("/wifi.json", "r");
    if (!f) {
        Serial.println("Failed to open wifi.json");

        logEvent("ERROR", "System", "Error opening wifi config (wifi.json)");
        return;
    }
    
    String json = f.readString();
    f.close();
    
    DynamicJsonDocument doc(256);
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.print("Error parsing wifi.json: ");
        Serial.println(err.c_str());

        logEvent("ERROR", "System", "Error reading wifi config from file (wifi.json)");
        return;
    }
    
    currentSSID = doc["ssid"].as<String>();
    // currentPassword = doc["password"].as<String>();
    
    // Validate SSID and password
    if (currentSSID.length() == 0 || currentSSID.length() > 32) {
        Serial.println("Invalid SSID length, using default");
        currentSSID = "Taronga Zoo Curlew Feeder";
    }
    
    // if (currentPassword.length() < 8 || currentPassword.length() > 63) {
    //     Serial.println("Invalid password length, using default");
    //     currentPassword = "12345678";
    // }
    
    Serial.println("Loaded WiFi settings:");
    Serial.println("  SSID: " + currentSSID);
    // Serial.println("  Password: " + String(currentPassword.length()) + " characters");
}

// ----------------------------------------
// Save WiFi settings to LittleFS
// ----------------------------------------
void saveWiFiSettings(String ssid /**, String password */) {
    File f = LittleFS.open("/wifi.json", "w");
    if (!f) {
        Serial.println("Failed to open wifi.json for writing");

        logEvent("ERROR", "System", "Error opening wifi config for writing (wifi.json)");
        return;
    }
    
    DynamicJsonDocument doc(256);
    doc["ssid"] = ssid;
    // doc["password"] = password;
    
    String jsonStr;
    serializeJson(doc, jsonStr);
    f.print(jsonStr);
    f.close();
    
    Serial.println("Saved WiFi settings: " + jsonStr);
}

// ----------------------------------------
// Add event to log
// ----------------------------------------
void logEvent(String type, String mode, String message) {
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

// ----------------------------------------
// Save single event to file (append mode)
// ----------------------------------------
void saveEventToFile(const EventLog &event) {
    File f = LittleFS.open("/events.log", "a");  // Append mode
    if (!f) {
        Serial.println("Failed to open events.log for writing");

        logEvent("ERROR", "System", "Error opening event log for writing (events.log)");
        return;
    }
    
    // Write in CSV format: timestamp,type,mode,message
    f.printf("%lu,%s,%s,%s\n", 
             event.timestamp, 
             event.type.c_str(), 
             event.mode.c_str(), 
             event.message.c_str());
    
    f.close();
}

// ----------------------------------------
// Load events from file (last 24 hours only)
// ----------------------------------------
void loadEventsFromFile() {
    eventHistory.clear();
    
    if (!LittleFS.exists("/events.log")) {
        Serial.println("events.log not found");

        logEvent("ERROR", "System", "No event log file found (events.log)");
        return;
    }
    
    File f = LittleFS.open("/events.log", "r");
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
    
    // Rewrite file with only recent events (cleanup old ones)
    f = LittleFS.open("/events.log", "w");
    if (f) {
        for (const auto &event : tempEvents) {
            f.printf("%lu,%s,%s,%s\n", 
                     event.timestamp, 
                     event.type.c_str(), 
                     event.mode.c_str(), 
                     event.message.c_str());
            
            // Also load into memory (keep last MAX_EVENTS_IN_MEMORY)
            if (eventHistory.size() < MAX_EVENTS_IN_MEMORY) {
                eventHistory.push_back(event);
            }
        }
        f.close();
    }
    
    Serial.printf("Loaded %d events from log file\n", eventHistory.size());
}

// ----------------------------------------
// Convert events to JSON (last 24 hours)
// ----------------------------------------
String eventsToJson() {
    DateTime now = rtc.now();
    uint32_t currentUnix = now.unixtime();
    uint32_t cutoffTime = currentUnix - EVENT_RETENTION_SECONDS;
    
    DynamicJsonDocument doc(8192);  // Larger buffer for events
    JsonArray arr = doc.to<JsonArray>();
    
    // Add events from newest to oldest (reverse order)
    for (int i = eventHistory.size() - 1; i >= 0; i--) {
        const EventLog &event = eventHistory[i];
        
        // Only include events from last 24 hours
        if (event.timestamp >= cutoffTime) {
            JsonObject o = arr.createNestedObject();
            o["timestamp"] = event.timestamp;
            o["type"] = event.type;
            o["mode"] = event.mode;
            o["message"] = event.message;
            
            // Format human-readable time
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

// ----------------------------------------
// Save mode configuration to LittleFS
// ----------------------------------------
void saveModeConfig() {
    File f = LittleFS.open("/mode.json", "w");
    if (!f) {
        Serial.println("Failed to open mode.json for writing");
        return;
    }
    
    DynamicJsonDocument doc(512);
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

// ----------------------------------------
// Load mode configuration from LittleFS
// ----------------------------------------
void loadModeConfig() {
    if (!LittleFS.exists("/mode.json")) {
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
    
    File f = LittleFS.open("/mode.json", "r");
    if (!f) {
        Serial.println("Failed to open mode.json");
        return;
    }
    
    String json = f.readString();
    f.close();
    
    DynamicJsonDocument doc(512);
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
    modeConfig.randIntervalBlockStartUnix = doc["randIntervalBlockStartUnix"];  // ✅ NEW
    modeConfig.randIntervalNextTriggerUnix = doc["randIntervalNextTriggerUnix"];
    
    Serial.println("Loaded mode config: " + json);
}

// ----------------------------------------
// Save alarms to LittleFS
// ----------------------------------------
void saveAlarms() {
    // Sort alarms by time before saving
    std::sort(alarms.begin(), alarms.end(), [](const Alarm &a, const Alarm &b) {
        return a.time < b.time;  // String comparison works for "HH:MM" format
    });

    File f = LittleFS.open("/alarms.json", "w");
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

// ----------------------------------------
// Load alarms from LittleFS
// ----------------------------------------
void loadAlarms() {
    if (!LittleFS.exists("/alarms.json")) {
        Serial.println("alarms.json not found, creating new file");
        alarms.clear();
        saveAlarms();
        return;
    }

    File f = LittleFS.open("/alarms.json", "r");
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

    DynamicJsonDocument doc(4096);
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

// ----------------------------------------
// Serve static files (HTML/CSS/JS)
// ----------------------------------------
void serveStaticFile(String path, String type) {
    setCORSHeaders();
    File f = LittleFS.open(path, "r");
    if (!f) {
        server.send(404, "text/plain", "File not found");
        return;
    }
    server.streamFile(f, type);
    f.close();
}

// ----------------------------------------
// Initialize settings.json if needed
// ----------------------------------------
void initSettings() {
    if (!LittleFS.exists("/settings.json")) {
        Serial.println("settings.json not found, creating default");
        File f = LittleFS.open("/settings.json", "w");
        if (f) {
            f.print("{\"timeFormat\":\"12\",\"theme\":\"light\"}");
            f.close();
        }
    }
}

// ------------------------ @todo: organise this! split into separate related functions (i.e. registerRoutesWiFi, registerRoutesMode, etc.) ------------------------

// ----------------------------------------
// Register all HTTP routes 
// ----------------------------------------
void registerRoutes() {

    // Captive Portal Detection For Some Android Devices
    server.on("/generate_204", HTTP_GET, []() {
        setCORSHeaders();
        server.sendHeader("Location", "http://" + WiFi.softAPIP().toString());
        server.send(302, "text/plain", "");
    });

    server.on("/gen_204", HTTP_GET, []() {
        setCORSHeaders();
        server.sendHeader("Location", "http://" + WiFi.softAPIP().toString());
        server.send(302, "text/plain", "");
    });

    server.on("/ncsi.txt", HTTP_GET, []() {
        setCORSHeaders();
        server.sendHeader("Location", "http://" + WiFi.softAPIP().toString());
        server.send(302, "text/plain", "");
    });

    server.on("/connecttest.txt", HTTP_GET, []() {
        setCORSHeaders();
        server.sendHeader("Location", "http://" + WiFi.softAPIP().toString());
        server.send(302, "text/plain", "");
    });

    server.on("/hotspot-detect.html", HTTP_GET, []() {
        setCORSHeaders();
        server.sendHeader("Location", "http://" + WiFi.softAPIP().toString());
        server.send(302, "text/plain", "");
    });

    // GET current servo position
    server.on("/api/servo", HTTP_GET, []() {
        setCORSHeaders();
        
        DynamicJsonDocument doc(256);
        doc["compartment"] = compartment;
        doc["angle"] = compartment * 60;
        doc["maxCompartment"] = maxCompartment;
        
        String json;
        serializeJson(doc, json);
        server.send(200, "application/json", json);
    });

    // GET current battery charge
    server.on("/api/battery", HTTP_GET, []() {
        setCORSHeaders();

        int batteryPercent = runBatteryCheck();

        DynamicJsonDocument doc(256);
        doc["battery"] = batteryPercent;

        String json;
        serializeJson(doc, json);
        server.send(200, "application/json", json);
    });

    server.on("/api/battery", HTTP_OPTIONS, []() {
        setCORSHeaders();
        server.send(200, "text/plain", "");
    });

    // GET event history
    server.on("/api/events", HTTP_GET, []() {
        setCORSHeaders();
        
        // Reload events from file to ensure we have latest
        loadEventsFromFile();
        
        String json = eventsToJson();
        Serial.println("GET /api/events -> " + String(eventHistory.size()) + " events");
        server.send(200, "application/json", json);
    });

    server.on("/api/events", HTTP_OPTIONS, []() {
        setCORSHeaders();
        server.send(200, "text/plain", "");
    });
    
    // DELETE all events (clear history)
    server.on("/api/events", HTTP_DELETE, []() {
        setCORSHeaders();
        
        eventHistory.clear();
        
        // Clear the log file
        LittleFS.remove("/events.log");
        
        Serial.println("Event history cleared");
        server.send(200, "application/json", "{\"status\":\"ok\"}");
    });
    
    // GET event statistics
    server.on("/api/events/stats", HTTP_GET, []() {
        setCORSHeaders();
        
        int successCount = 0;
        int errorCount = 0;
        
        for (const auto &event : eventHistory) {
            if (event.type == "SUCCESS") {
                successCount++;
            } else if (event.type == "ERROR") {
                errorCount++;
            }
        }
        
        DynamicJsonDocument doc(256);
        doc["totalEvents"] = eventHistory.size();
        doc["successCount"] = successCount;
        doc["errorCount"] = errorCount;
        doc["retentionHours"] = EVENT_RETENTION_SECONDS / 3600;
        
        String json;
        serializeJson(doc, json);
        server.send(200, "application/json", json);
    });

    server.on("/api/events/stats", HTTP_OPTIONS, []() {
        setCORSHeaders();
        server.send(200, "text/plain", "");
    });

    // Handle OPTIONS requests for CORS
    server.on("/api/alarms/", HTTP_OPTIONS, []() {
        setCORSHeaders();
        server.send(200, "text/plain", "");
    });

    server.on("/api/settings", HTTP_OPTIONS, []() {
        setCORSHeaders();
        server.send(200, "text/plain", "");
    });

    // Root = index.html
    server.on("/", HTTP_GET, []() {
        serveStaticFile("/index.html", "text/html");
    });

    // Static assets
    server.on("/style.css", HTTP_GET, []() { 
        serveStaticFile("/style.css", "text/css"); 
    });
    
    server.on("/script.js", HTTP_GET, []() { 
        serveStaticFile("/script.js", "application/javascript"); 
    });

    server.on("/taronga-zoo-logo.png", HTTP_GET, []() {
        serveStaticFile("/taronga-zoo-logo.png", "image/png");
    });

    // GET alarms
    server.on("/api/alarms", HTTP_GET, []() {
        setCORSHeaders();
        String json = alarmsToJson();
        Serial.println("GET /api/alarms -> " + json);
        server.send(200, "application/json", json);
    });

    // POST add alarm
    server.on("/api/alarms", HTTP_POST, []() {
        setCORSHeaders();
        
        String body = server.arg("plain");
        Serial.println("POST /api/alarms body: " + body);
        
        DynamicJsonDocument doc(256);
        DeserializationError err = deserializeJson(doc, body);
        
        if (err) {
            Serial.print("Error parsing POST data: ");
            Serial.println(err.c_str());

            logEvent("ERROR", "System", "Error parsing set-time addition request");
            server.send(400, "text/plain", "Invalid JSON");
            return;
        }

        Alarm a;
        a.id = millis();
        a.time = doc["time"].as<String>();
        a.active = true;

        alarms.push_back(a);
        saveAlarms();

        String json = alarmsToJson();
        Serial.println("POST response: " + json);
        server.send(200, "application/json", json);
    });


    // SETTINGS GET
    server.on("/api/settings", HTTP_GET, []() {
        setCORSHeaders();
        Serial.println("GET /api/settings");
        serveStaticFile("/settings.json", "application/json");
    });

    // SETTINGS POST
    server.on("/api/settings", HTTP_POST, []() {
        setCORSHeaders();
        
        String body = server.arg("plain");
        Serial.println("POST /api/settings body: " + body);
        
        File f = LittleFS.open("/settings.json", "w");
        if (!f) {
            Serial.println("Failed to save settings");

            logEvent("ERROR", "System", "Error saving settings to file (settings.json)");
            server.send(500, "text/plain", "Failed to save settings");
            return;
        }
        f.print(body);
        f.close();
        
        Serial.println("Settings saved successfully");
        server.send(200, "text/plain", "OK");
    });

    // Manual Activation
    server.on("/api/trigger-now", HTTP_POST, []() {
        setCORSHeaders();

        // trigger servo activation with noMode set to true
        triggerActivation(true);

        server.send(200, "text/plain", "OK");
    });

    server.on("/api/trigger-now", HTTP_OPTIONS, []() {
        setCORSHeaders();
        server.send(200, "text/plain", "");
    });

    // Reset Chamber positions (reset current chamber to 0) -> for refilling/ease of use
    server.on("/api/reset-motor", HTTP_POST, []() {
        setCORSHeaders();

        Serial.println("Resetting Motor Position. Moving to Angle 0 (Dead Chamber).");
        moveToAngle(0);

        compartment = 0;
        saveCompartmentPosition();


        server.send(200, "text/plain", "OK");
    });

    server.on("/api/reset-motor", HTTP_OPTIONS, []() {
        setCORSHeaders();
        server.send(200, "text/plain", "");
    });

    // GET current time from RTC
    server.on("/api/time", HTTP_GET, []() {
        setCORSHeaders();
        
        DateTime now = rtc.now();
        
        DynamicJsonDocument doc(128);
        doc["hour"] = now.hour();
        doc["minute"] = now.minute();
        doc["second"] = now.second();
        doc["date"] = String(now.year()) + "-" + 
                    String(now.month()) + "-" + 
                    String(now.day());
        
        String json;
        serializeJson(doc, json);
        
        server.send(200, "application/json", json);
    });

    server.on("/api/time", HTTP_OPTIONS, []() {
        setCORSHeaders();
        server.send(200, "text/plain", "");
    });

    // POST set mode to "set_times"
    server.on("/api/mode/set-times", HTTP_POST, []() {
        setCORSHeaders();
        
        modeConfig.activeMode = "set_times";
        saveModeConfig();
        
        server.send(200, "application/json", "{\"status\":\"ok\"}");
    });

    // GET mode configuration
    server.on("/api/mode", HTTP_GET, []() {
        setCORSHeaders();
        
        DynamicJsonDocument doc(512);
        doc["activeMode"] = modeConfig.activeMode;
        doc["regIntervalHours"] = modeConfig.regIntervalHours;
        doc["regIntervalMinutes"] = modeConfig.regIntervalMinutes;
        doc["randIntervalHours"] = modeConfig.randIntervalHours;
        doc["randIntervalMinutes"] = modeConfig.randIntervalMinutes;
        
        // Calculate next activation time based on mode
        DateTime now = rtc.now(); // AEST
        uint32_t currentUnix = now.unixtime();
        String nextTime = "";
        
        if (modeConfig.activeMode == "set_times") {
            // Find next alarm
            for (auto &a : alarms) {
                if (a.active) {
                    int alarmHour = a.time.substring(0, 2).toInt();
                    int alarmMin = a.time.substring(3, 5).toInt();
                    
                    // Check if alarm is later today
                    if (alarmHour > now.hour() || 
                        (alarmHour == now.hour() && alarmMin > now.minute())) {
                        nextTime = a.time;
                        break;
                    }
                }
            }
            if (nextTime == "" && alarms.size() > 0) {
                // Wrap to tomorrow's first alarm
                for (auto &a : alarms) {
                    if (a.active) {
                        nextTime = a.time + " (tomorrow)";
                        break;
                    }
                }
            }
        } 
        else if (modeConfig.activeMode == "regular_interval") {
            if (modeConfig.regIntervalLastTriggerUnix > 0) {
                uint32_t intervalSeconds = (modeConfig.regIntervalHours * 3600UL + 
                                            modeConfig.regIntervalMinutes * 60UL);
                uint32_t nextTriggerUnix = modeConfig.regIntervalLastTriggerUnix + intervalSeconds;
                
                if (currentUnix >= nextTriggerUnix) {
                    nextTime = "Overdue";
                } else {
                    uint32_t remaining = nextTriggerUnix - currentUnix;
                    int remainingHours = remaining / 3600;
                    int remainingMinutes = (remaining % 3600) / 60;
                    nextTime = String(remainingHours) + "h " + String(remainingMinutes) + "m";
                }
            } else {
                nextTime = "Not started";
            }
        } 
        else if (modeConfig.activeMode == "random_interval") {
            if (modeConfig.randIntervalNextTriggerUnix > 0) {
                if (currentUnix >= modeConfig.randIntervalNextTriggerUnix) {
                    nextTime = "Overdue";
                } else {
                    uint32_t remaining = modeConfig.randIntervalNextTriggerUnix - currentUnix;
                    int remainingHours = remaining / 3600;
                    int remainingMinutes = (remaining % 3600) / 60;
                    nextTime = String(remainingHours) + "h " + String(remainingMinutes) + "m (random)";
                }
            } else {
                nextTime = "Not started";
            }
        }
        
        doc["nextActivationTime"] = nextTime;
        
        String json;
        serializeJson(doc, json);
        server.send(200, "application/json", json);
    });

    // POST set mode to "regular_interval"
    server.on("/api/mode/regular-interval", HTTP_POST, []() {
        setCORSHeaders();
        
        String body = server.arg("plain");
        DynamicJsonDocument doc(256);
        deserializeJson(doc, body);
        
        modeConfig.activeMode = "regular_interval";
        modeConfig.regIntervalHours = doc["hours"];
        modeConfig.regIntervalMinutes = doc["minutes"];
        
        // Initialize with current time as last trigger
        DateTime now = rtc.now();
        modeConfig.regIntervalLastTriggerUnix = now.unixtime();
        
        saveModeConfig();
        
        Serial.printf("Regular interval set: %dh %dm, starting from now\n",
                    modeConfig.regIntervalHours, modeConfig.regIntervalMinutes);
        
        server.send(200, "application/json", "{\"status\":\"ok\"}");
    });

    // POST set mode to "random_interval"
    server.on("/api/mode/random-interval", HTTP_POST, []() {
        setCORSHeaders();
        
        String body = server.arg("plain");
        DynamicJsonDocument doc(256);
        deserializeJson(doc, body);
        
        modeConfig.activeMode = "random_interval";
        modeConfig.randIntervalHours = doc["hours"];
        modeConfig.randIntervalMinutes = doc["minutes"];
        
        initializeRandomInterval();
        
        server.send(200, "application/json", "{\"status\":\"ok\"}");
    });

    // POST trigger sleep mode
    server.on("/api/sleep", HTTP_POST, []() {
        setCORSHeaders();
        server.send(200, "application/json", "{\"status\":\"sleeping\"}");
        
        delay(500); // Give response time to send
        
        Serial.println("Manual sleep requested via API");
        server.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);

        // AP Mode off -> Turn LED OFF
        digitalWrite(LED_PIN, LOW);
        
        delay(100);
        configureNextWake();
        enterDeepSleep();
    });

    // POST sync time from browser/device
    server.on("/api/sync-time", HTTP_POST, []() {
        setCORSHeaders();
        
        String body = server.arg("plain");
        Serial.println("POST /api/sync-time body: " + body);
        
        DynamicJsonDocument doc(256);
        DeserializationError error = deserializeJson(doc, body);
        
        if (error) {
            Serial.print("Error parsing sync-time JSON: ");
            Serial.println(error.c_str());

            logEvent("ERROR", "System", "Error parsing sync-time request");
            server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
            return;
        }
        
        // Get timestamp in milliseconds from JavaScript (already in AEST)
        long long timestampMs = doc["timestamp"];
        
        // Convert to seconds
        time_t epoch = timestampMs / 1000;
        
        // Create DateTime object (this is AEST time)
        DateTime newTime(epoch);
        
        // Set the RTC to AEST time
        rtc.adjust(newTime);
        
        Serial.printf("RTC time synced to AEST: %04d-%02d-%02d %02d:%02d:%02d\n",
                    newTime.day(), newTime.month(), newTime.year(),
                    newTime.hour(), newTime.minute(), newTime.second());
        
        server.send(200, "application/json", "{\"success\":true}");
    });

    server.on("/api/sync-time", HTTP_OPTIONS, []() {
        setCORSHeaders();
        server.send(200, "text/plain", "");
    });

    // GET WiFi settings
    server.on("/api/wifi", HTTP_GET, []() {
        setCORSHeaders();
        Serial.println("GET /api/wifi");
        
        DynamicJsonDocument doc(256);
        doc["ssid"] = currentSSID;
        // doc["password"] = currentPassword;
        
        String json;
        serializeJson(doc, json);
        server.send(200, "application/json", json);
    });

    server.on("/api/wifi", HTTP_OPTIONS, []() {
        setCORSHeaders();
        server.send(200, "text/plain", "");
    });

    // POST update WiFi settings
    server.on("/api/wifi", HTTP_POST, []() {
        setCORSHeaders();
        
        String body = server.arg("plain");
        Serial.println("POST /api/wifi body: " + body);
        
        DynamicJsonDocument doc(256);
        DeserializationError err = deserializeJson(doc, body);
        
        if (err) {
            Serial.print("Error parsing WiFi settings: ");
            Serial.println(err.c_str());

            logEvent("ERROR", "System", "Error parsing WiFi settings");
            server.send(400, "text/plain", "Invalid JSON");
            return;
        }
        
        String newSSID = doc["ssid"].as<String>();
        // String newPassword = doc["password"].as<String>();
        
        // Validate SSID
        if (newSSID.length() == 0 || newSSID.length() > 32) {
            server.send(400, "application/json", 
                "{\"error\":\"SSID must be 1-32 characters\"}");
            return;
        }
        
        // Validate password
        // if (newPassword.length() < 8 || newPassword.length() > 63) {
        //     server.send(400, "application/json", 
        //         "{\"error\":\"Password must be 8-63 characters\"}");
        //     return;
        // }
        
        // Save to file
        // saveWiFiSettings(newSSID, newPassword);
        saveWiFiSettings(newSSID);
        
        // Update current settings
        currentSSID = newSSID;
        // currentPassword = newPassword;
        
        Serial.println("WiFi settings updated successfully");
        Serial.println("  New SSID: " + currentSSID);
        // Serial.println("  Password length: " + String(currentPassword.length()));
        
        // Send success response with info about restart requirement
        server.send(200, "application/json", 
            "{\"status\":\"ok\",\"message\":\"Settings saved. Changes will apply on next wake/restart.\"}");
    });


    // 404 fallback (important for captive portal)
    server.onNotFound([]() {
        String uri = server.uri();
        HTTPMethod method = server.method();
        
        // Handle DELETE /api/alarms/{id}
        if (uri.startsWith("/api/alarms/") && method == HTTP_DELETE) {
            setCORSHeaders();
            
            int lastSlash = uri.lastIndexOf('/');
            String idStr = uri.substring(lastSlash + 1);
            uint32_t id = idStr.toInt();
            
            Serial.printf("DELETE request for alarm ID: %u\n", id);
            
            size_t before = alarms.size();
            alarms.erase(
                std::remove_if(alarms.begin(), alarms.end(),
                    [id](const Alarm &a){ return a.id == id; }),
                alarms.end()
            );
            
            Serial.printf("Deleted alarm. Count: %d -> %d\n", before, alarms.size());
            saveAlarms();
            server.send(200, "application/json", alarmsToJson());
            return;
        }
        
        // Handle PATCH /api/alarms/{id}
        if (uri.startsWith("/api/alarms/") && method == HTTP_PATCH) {
            setCORSHeaders();
            
            int lastSlash = uri.lastIndexOf('/');
            String idStr = uri.substring(lastSlash + 1);
            uint32_t id = idStr.toInt();
            
            Serial.printf("PATCH request for alarm ID: %u\n", id);
            
            bool found = false;
            for (auto &a : alarms) {
                if (a.id == id) {
                    a.active = !a.active;
                    found = true;
                    Serial.printf("Toggled alarm %u to %s\n", id, a.active ? "ON" : "OFF");
                    break;
                }
            }
            
            if (!found) {
                Serial.printf("Alarm %u not found\n", id);
            }
            
            saveAlarms();
            server.send(200, "application/json", alarmsToJson());
            return;
        }
        
        // Handle OPTIONS /api/alarms/{id}
        if (uri.startsWith("/api/alarms/") && method == HTTP_OPTIONS) {
            setCORSHeaders();
            server.send(200, "text/plain", "");
            return;
        }
        
        // Silently ignore common requests
        if (uri == "/favicon.ico") {
            server.send(204, "text/plain", "");
            return;
        }

        // Redirect captive portal detection URLs (fallback)
        if (uri.indexOf("generate_204") >= 0 || 
            uri.indexOf("gen_204") >= 0 ||
            uri.indexOf("ncsi") >= 0 ||
            uri.indexOf("connecttest") >= 0) {
            server.sendHeader("Location", "http://" + WiFi.softAPIP().toString());
            server.send(302, "text/plain", "");
            return;
        }
        
        // Default 404 - serve index.html (for captive portal)
        Serial.println("404: " + uri);

        server.sendHeader("Location", "http://" + WiFi.softAPIP().toString());
        server.send(302, "text/html", "");
    });
}

// ----------------------------------------
// Captive Portal setup
// ----------------------------------------
void setupCaptivePortal() {
    // Use the loaded SSID and password
    // WiFi.softAP(currentSSID.c_str(), currentPassword.c_str());

    // Initialise access point with current name and no password
    WiFi.softAP(currentSSID.c_str(), NULL);

    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

    Serial.print("AP running. Connect to: ");
    Serial.println(currentSSID.c_str());
    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());
}

// ----------------------------------------
// Check all trigger conditions (while AP mode is running)
// ----------------------------------------
void checkTriggers() {
    static unsigned long lastCheck = 0;
    unsigned long now = millis();
    
    // Only check once per second
    if (now - lastCheck < 1000) {
        return;
    }
    lastCheck = now;

    DateTime rtcTime = rtc.now(); // AEST time
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
            // If first run, initialise
            if (modeConfig.regIntervalLastTriggerUnix == 0) {
                modeConfig.regIntervalLastTriggerUnix = currentUnix;
                saveModeConfig();
                Serial.println("Regular interval initialized");
            }
            
            // Calculate next trigger time
            uint32_t nextTriggerUnix = modeConfig.regIntervalLastTriggerUnix + intervalSeconds;
            
            // Check if it's time to trigger
            if (currentUnix >= nextTriggerUnix) {
                Serial.printf("REGULAR INTERVAL: Triggered after %dh %dm\n", 
                             modeConfig.regIntervalHours, modeConfig.regIntervalMinutes);
                
                triggerActivation();
                
                // Update last trigger time
                modeConfig.regIntervalLastTriggerUnix = currentUnix;
                saveModeConfig();
            }
        }
    }
    
    // MODE 3: Random Interval
    else if (modeConfig.activeMode == "random_interval") {
        // Initialise if not set
        if (modeConfig.randIntervalNextTriggerUnix == 0 || 
            modeConfig.randIntervalBlockStartUnix == 0) {
            initializeRandomInterval();
            return;
        }
        
        // Check if it's time to trigger
        if (currentUnix >= modeConfig.randIntervalNextTriggerUnix) {
            Serial.printf("RANDOM INTERVAL: Triggered at random time within %dh %dm window\n",
                         modeConfig.randIntervalHours, modeConfig.randIntervalMinutes);
            
            triggerActivation();
            
            calculateNextRandomInterval();
        }
    }
}

// ----------------------------------------
// Actuator trigger function
// ----------------------------------------
void triggerActivation(bool noMode) {
    Serial.println("========================================");
    Serial.println("TRIGGER EVENT!");
    Serial.printf("Mode: %s\n", modeConfig.activeMode.c_str());
    
    DateTime now = rtc.now();
    Serial.printf("Time: %02d:%02d:%02d\n", now.hour(), now.minute(), now.second());
    
    // TODO: Add your actual trigger logic here
    // triggerServo();
    // activateRelay();
    // sendNotification();
    // logEvent();
    
    advanceCompartment();

    bool success = true;
    String errorMessage = "";
    String warning = "";
    
    
    if (!LittleFS.begin()) {
        warning = "LittleFS not accessible - compartment position may not persist";
        Serial.println("WARNING: " + warning);
        logEvent("WARNING", modeConfig.activeMode, warning);
        // Continue anyway - servo can still operate
    }

    // Check if RTC is still working
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

    String compartmentActivationStr = String(compartment + 1); // +1 to fix index
    if (compartment == 0) compartmentActivationStr = "6"; // if compartment is currently 0 (1st index), chamber 6 was just activated
    
    // log previous chamber successful completion
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

// ----------------------------------------
// Servo angle position function
// ----------------------------------------
void moveToAngle(int angle) {
  //angle = constrain(angle, 0, 360);

  float newAngle = angle;
  float oldAngle = angle - (360 / maxCompartment);

//   for (int i = oldAngle; i <= newAngle; i = i + 5) {
    // int pulse = map(i, 0, MECH_RANGE, minPulse, maxPulse);
//     myServo.writeMicroseconds(pulse);
//   }

    int pulse = map(angle, 0, MECH_RANGE, minPulse, maxPulse);
    myServo.writeMicroseconds(pulse);

//   Serial.print("Angle: ");
//   Serial.print(angle);
//   Serial.print("  Pulse: ");
//   Serial.println(pulse);
}

void advanceCompartment() {
  // Move to current compartment

  loadCompartmentPosition();

  Serial.println(compartment);
  int offset = 5;
  int angle = (compartment + 1) * 60 + offset;

  if (angle >= 300) {
    moveToAngle(angle);
    delay(2000);          // allow last item to drop
    compartment = 0;

    moveToAngle(0);      // return to deadspace
    saveCompartmentPosition();
    return;
  }

  moveToAngle(angle);

  compartment++;
  saveCompartmentPosition();
}

float checkVoltage() {
  float busvoltage = ina219.getBusVoltage_V(); // Reads supply voltage
  return busvoltage; 
}

int voltageToSOC(float v) { //piecewise approximation of the battery's charge
  if (v >= 8.25) return 100;
  if (v >= 8.10) return 90;
  if (v >= 7.90) return 80;
  if (v >= 7.70) return 70;
  if (v >= 7.50) return 60;
  if (v >= 7.30) return 50;
  if (v >= 7.10) return 40;
  if (v >= 6.90) return 30;
  if (v >= 6.70) return 20;
  if (v >= 6.50) return 10;
  return 0;
}

int runBatteryCheck() {
  float busvoltage = checkVoltage();
  int batteryPercent = voltageToSOC(busvoltage);
  Serial.print("Supply Voltage: "); Serial.print(busvoltage); Serial.println(" V");
  Serial.print("Battery Charge: "); Serial.print(batteryPercent); Serial.println(" %");

  return batteryPercent;
}

// ----------------------------------------
// Setup
// ----------------------------------------
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n=== ESP32 Alarm System Starting ===");
    
    // Configure pins
    pinMode(RTC_ALARM_PIN, INPUT_PULLUP);
    pinMode(BUTTON_PIN, INPUT);
    pinMode(LED_PIN, OUTPUT);
    pinMode(SERVO_TRANSISTOR_PIN, OUTPUT);

    // turn on transistor
    digitalWrite(SERVO_TRANSISTOR_PIN, HIGH);

    myServo.attach(servoPin); // Attach the servo to the pin
    myServo.setPeriodHertz(50);

    // Start file system (including for logging events)
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed");
        logEvent("ERROR", "System", "Flash Memory (LittleFS) error on startup");
        return;
    }
    Serial.println("LittleFS mounted");
    
    // Check wake reason
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    if (!rtc.begin(&Wire)) {
        Serial.println("RTC not found!");
        logEvent("ERROR", "System", "RTC communication error on startup - clock may have lost power");
    } else {
        Serial.println("RTC initialized");
        
        // Clear any pending alarms
        rtc.clearAlarm(1);
        rtc.clearAlarm(2);
        
        // Only adjust time on first boot
        if (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED) {
            rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        }
        
        DateTime now = rtc.now();
        Serial.printf("Current RTC time: %02d:%02d:%02d\n", 
                      now.hour(), now.minute(), now.second());
    }

    if (!ina219.begin()) {
        Serial.println("Failed to find INA219 chip");
        logEvent("ERROR", "System", "Failed to find INA219 (battery sensor) on startup");
        // while (1) { delay(10); }
    }
    Serial.println("INA219 (Battery Sensor) Found");

    runBatteryCheck();

    loadCompartmentPosition();

    loadWiFiSettings();

    initSettings();

    alarms.clear();
    loadAlarms();
    
    loadModeConfig();

    // Load event history
    loadEventsFromFile();
    
    
    Serial.print("Wake reason: ");
    switch(wakeup_reason) {
        case ESP_SLEEP_WAKEUP_EXT0:
            apModeActive = false;

            break;
        case ESP_SLEEP_WAKEUP_EXT1:
                // Serial.println("WAKEUP_EXT1");
            Serial.println("Button wake detected - starting AP mode");

            delay(300);
            // if button is still low after delay, false alarm -> go back to sleep
            if (digitalRead(BUTTON_PIN) == 0) {
                configureNextWake();
                enterDeepSleep();
                // break;
            }

            // Log startup from button event
            logEvent("SUCCESS", "System", "System started/woke from sleep");

            apModeActive = true;
            apStartTime = millis();
            break;
        case ESP_SLEEP_WAKEUP_TIMER:
            // not used
            Serial.println("Timer wake");
            break;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        default:
            Serial.println("Not from deep sleep (first boot or reset)");
            // First boot - start in AP mode for initial configuration

            logEvent("SUCCESS", "System", "Inital system start");

            apModeActive = true;
            apStartTime = millis();
            break;
    }
    
    // If woken by RTC alarm (not button), trigger the event immediately
    if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("\nRTC alarm wake - triggering scheduled event...");

        DateTime now = rtc.now();
        uint32_t currentUnix = now.unixtime();
        
        triggerActivation();
        
        // Update trigger times based on mode
        if (modeConfig.activeMode == "set_times") {
            // No update needed - alarms are static times
            Serial.println("Set times mode - no update needed");
        }
        else if (modeConfig.activeMode == "regular_interval") {
            // Update last trigger time to now
            modeConfig.regIntervalLastTriggerUnix = currentUnix;
            saveModeConfig();
            Serial.printf("Updated last trigger to: %lu\n", currentUnix);
        }
        else if (modeConfig.activeMode == "random_interval") {
            // calculate next interval block
            calculateNextRandomInterval();
        }
        
        // Go back to sleep immediately after triggering
        configureNextWake();
        delay(1000);
        enterDeepSleep();
    }
    
    // Start AP mode if needed (button wake or first boot)
    if (apModeActive) {
        setupCaptivePortal();
        registerRoutes();
        server.begin();

        // Web Server on -> Turn LED ON
        digitalWrite(LED_PIN, HIGH);

        Serial.println("Web server started.");
        Serial.printf("AP mode will timeout in %lu minutes\n", AP_TIMEOUT_MS / 60000);
    }
    
    Serial.println("================================");
}

// ----------------------------------------
// Loop
// ----------------------------------------
void loop() {
    if (apModeActive) {
        // Handle web server in AP mode
        dnsServer.processNextRequest();
        server.handleClient();
        checkTriggers();
        
        // Check if AP timeout has expired
        if (millis() - apStartTime >= AP_TIMEOUT_MS) {
            Serial.println("\n>>> AP mode timeout - preparing for sleep <<<");
            
            // Shutdown cleanly
            server.stop();
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_OFF);

            // AP Mode off -> Turn LED OFF
            digitalWrite(LED_PIN, LOW);
            
            delay(100);
            
            // Configure next wake and sleep
            configureNextWake();
            enterDeepSleep();
        }
        
        // Show countdown every 60 seconds
        static unsigned long lastCountdown = 0;
        if (millis() - lastCountdown >= 60000) {
            unsigned long remaining = AP_TIMEOUT_MS - (millis() - apStartTime);
            Serial.printf("AP mode time remaining: %lu minutes\n", remaining / 60000);

            // todo: trigger notification on web server
            // todo: add button to extend time on web server (maybe)

            lastCountdown = millis();
        }
    } else {
        // Should never reach here in normal operation
        // But if we do, configure sleep
        delay(100);
        configureNextWake();
        enterDeepSleep();
    }
    
    delay(100);
}