#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <RTClib.h>
#include <algorithm>
#include <esp_sleep.h>  // Add this for deep sleep functions

#define I2C_SDA_PIN 33
#define I2C_SCL_PIN 32
#define RTC_ALARM_PIN 25    // DS3231 SQW pin connected 
#define BUTTON_PIN 34       // Button for manual wake (pull-up)

// AP timeout settings
#define AP_TIMEOUT_MS 900000UL  // 15 minutes in milliseconds
unsigned long apStartTime = 0;
bool apModeActive = false;


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
    uint32_t randIntervalNextTriggerUnix; // Unix timestamp (AEST) when to trigger
};

ModeConfig modeConfig;

std::vector<Alarm> alarms;


// ----------------------------------------
// Calculate next wake time and configure RTC alarm
// ----------------------------------------
void configureNextWake() {
    DateTime now = rtc.now(); // AEST time
    DateTime nextWake;
    bool alarmSet = false;
    
    Serial.println("\n=== Configuring Next Wake ===");
    Serial.printf("Current time (AEST): %04d-%02d-%02d %02d:%02d:%02d\n",
                 now.year(), now.month(), now.day(),
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
        
        if (modeConfig.randIntervalNextTriggerUnix > 0) {
            uint32_t currentUnix = now.unixtime();
            
            // If we're past the trigger time (overdue)
            if (currentUnix >= modeConfig.randIntervalNextTriggerUnix) {
                Serial.println("Overdue - triggering soon");
                nextWake = DateTime(currentUnix + 60);
            } else {
                // Normal case - use the pre-calculated random time
                nextWake = DateTime(modeConfig.randIntervalNextTriggerUnix);
                uint32_t remaining = modeConfig.randIntervalNextTriggerUnix - currentUnix;
                Serial.printf("Next trigger in %lu seconds (%lu minutes)\n", 
                             remaining, remaining / 60);
            }
            alarmSet = true;
        } else {
            Serial.println("No random trigger time set");
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
    
    
    // Configure wake sources
    // esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN, HIGH);     // Button press
    esp_sleep_enable_ext1_wakeup(1ULL << BUTTON_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)RTC_ALARM_PIN, 0);  // RTC alarm (SQW goes LOW)
    

    
    Serial.println("Wake sources configured:");
    Serial.printf("  - RTC Alarm on GPIO %d\n", RTC_ALARM_PIN);
    Serial.printf("  - Button on GPIO %d\n", BUTTON_PIN);
    Serial.println("Entering deep sleep NOW...");
    Serial.println("========================================\n");
    
    delay(100); // Give serial time to finish
    
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
// Load WiFi settings from LittleFS
// ----------------------------------------
void loadWiFiSettings() {
    if (!LittleFS.exists("/wifi.json")) {
        Serial.println("wifi.json not found, creating default");
        File f = LittleFS.open("/wifi.json", "w");
        if (f) {
            f.print("{\"ssid\":\"Taronga Zoo Curlew Feeder\",\"password\":\"12345678\"}");
            f.close();
        }
        return;
    }
    
    File f = LittleFS.open("/wifi.json", "r");
    if (!f) {
        Serial.println("Failed to open wifi.json");
        return;
    }
    
    String json = f.readString();
    f.close();
    
    DynamicJsonDocument doc(256);
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.print("Error parsing wifi.json: ");
        Serial.println(err.c_str());
        return;
    }
    
    currentSSID = doc["ssid"].as<String>();
    currentPassword = doc["password"].as<String>();
    
    // Validate SSID and password
    if (currentSSID.length() == 0 || currentSSID.length() > 32) {
        Serial.println("Invalid SSID length, using default");
        currentSSID = "Taronga Zoo Curlew Feeder";
    }
    
    if (currentPassword.length() < 8 || currentPassword.length() > 63) {
        Serial.println("Invalid password length, using default");
        currentPassword = "12345678";
    }
    
    Serial.println("Loaded WiFi settings:");
    Serial.println("  SSID: " + currentSSID);
    Serial.println("  Password: " + String(currentPassword.length()) + " characters");
}

// ----------------------------------------
// Save WiFi settings to LittleFS
// ----------------------------------------
void saveWiFiSettings(String ssid, String password) {
    File f = LittleFS.open("/wifi.json", "w");
    if (!f) {
        Serial.println("Failed to open wifi.json for writing");
        return;
    }
    
    DynamicJsonDocument doc(256);
    doc["ssid"] = ssid;
    doc["password"] = password;
    
    String jsonStr;
    serializeJson(doc, jsonStr);
    f.print(jsonStr);
    f.close();
    
    Serial.println("Saved WiFi settings: " + jsonStr);
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
            f.print("{\"timeFormat\":\"24\",\"theme\":\"light\"}");
            f.close();
        }
    }
}

// ------------------------ @todo: organise this! split into separate related functions (i.e. registerRoutesWiFi, registerRoutesMode, etc.) ------------------------

// ----------------------------------------
// Register all HTTP routes 
// ----------------------------------------
void registerRoutes() {

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
            server.send(500, "text/plain", "Failed to save settings");
            return;
        }
        f.print(body);
        f.close();
        
        Serial.println("Settings saved successfully");
        server.send(200, "text/plain", "OK");
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
        
        // Calculate random trigger time within interval
        DateTime now = rtc.now();
        uint32_t currentUnix = now.unixtime();
        uint32_t intervalSeconds = (modeConfig.randIntervalHours * 3600UL + 
                                    modeConfig.randIntervalMinutes * 60UL);
        uint32_t randomOffset = random(0, intervalSeconds);
        
        modeConfig.randIntervalNextTriggerUnix = currentUnix + randomOffset;
        
        saveModeConfig();
        
        Serial.printf("Random interval set: %dh %dm, next trigger in %lu seconds\n",
                    modeConfig.randIntervalHours, modeConfig.randIntervalMinutes, randomOffset);
        
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
                    newTime.year(), newTime.month(), newTime.day(),
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
        doc["password"] = currentPassword;
        
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
            server.send(400, "text/plain", "Invalid JSON");
            return;
        }
        
        String newSSID = doc["ssid"].as<String>();
        String newPassword = doc["password"].as<String>();
        
        // Validate SSID
        if (newSSID.length() == 0 || newSSID.length() > 32) {
            server.send(400, "application/json", 
                "{\"error\":\"SSID must be 1-32 characters\"}");
            return;
        }
        
        // Validate password
        if (newPassword.length() < 8 || newPassword.length() > 63) {
            server.send(400, "application/json", 
                "{\"error\":\"Password must be 8-63 characters\"}");
            return;
        }
        
        // Save to file
        saveWiFiSettings(newSSID, newPassword);
        
        // Update current settings
        currentSSID = newSSID;
        currentPassword = newPassword;
        
        Serial.println("WiFi settings updated successfully");
        Serial.println("  New SSID: " + currentSSID);
        Serial.println("  Password length: " + String(currentPassword.length()));
        
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
        if (uri == "/favicon.ico" || uri == "/generate_204") {
            server.send(204, "text/plain", "");
            return;
        }
        
        // Default 404 - serve index.html (for captive portal)
        Serial.println("404: " + uri);
        serveStaticFile("/index.html", "text/html");
    });
}

// ----------------------------------------
// Captive Portal setup
// ----------------------------------------
void setupCaptivePortal() {
    // Use the loaded SSID and password
    WiFi.softAP(currentSSID.c_str(), currentPassword.c_str());

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
                doSomething();
                break;
            }
        }
    }
    
    // MODE 2: Regular Interval
    else if (modeConfig.activeMode == "regular_interval") {
        uint32_t intervalSeconds = (modeConfig.regIntervalHours * 3600UL + 
                                     modeConfig.regIntervalMinutes * 60UL);
        
        if (intervalSeconds > 0) {
            // If first run, initialize
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
                
                doSomething();
                
                // Update last trigger time
                modeConfig.regIntervalLastTriggerUnix = currentUnix;
                saveModeConfig();
            }
        }
    }
    
    // MODE 3: Random Interval
    else if (modeConfig.activeMode == "random_interval") {
        // If no trigger time set, initialize one
        if (modeConfig.randIntervalNextTriggerUnix == 0) {
            uint32_t intervalSeconds = (modeConfig.randIntervalHours * 3600UL + 
                                        modeConfig.randIntervalMinutes * 60UL);
            uint32_t randomOffset = random(0, intervalSeconds);
            modeConfig.randIntervalNextTriggerUnix = currentUnix + randomOffset;
            saveModeConfig();
            
            Serial.printf("Random interval initialized: trigger in %lu seconds\n", randomOffset);
        }
        
        // Check if it's time to trigger
        if (currentUnix >= modeConfig.randIntervalNextTriggerUnix) {
            Serial.printf("RANDOM INTERVAL: Triggered at random time within %dh %dm window\n",
                         modeConfig.randIntervalHours, modeConfig.randIntervalMinutes);
            
            doSomething();
            
            // Calculate next random trigger time
            uint32_t intervalSeconds = (modeConfig.randIntervalHours * 3600UL + 
                                        modeConfig.randIntervalMinutes * 60UL);
            uint32_t randomOffset = random(0, intervalSeconds);
            modeConfig.randIntervalNextTriggerUnix = currentUnix + randomOffset;
            
            saveModeConfig();
            
            Serial.printf("Next random trigger in %lu seconds (%.1f minutes)\n", 
                         randomOffset, randomOffset / 60.0);
        }
    }
}

// ----------------------------------------
// Actuator trigger function
// ----------------------------------------
void doSomething() {
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
    
    Serial.println("========================================");
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
    
    // Check wake reason
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    if (!rtc.begin(&Wire)) {
        Serial.println("RTC not found!");
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

    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed");
        return;
    }
    Serial.println("LittleFS mounted");

    loadWiFiSettings();

    initSettings();

    alarms.clear();
    loadAlarms();
    
    loadModeConfig();
    
    Serial.print("Wake reason: ");
    switch(wakeup_reason) {
        case ESP_SLEEP_WAKEUP_EXT0:

            Serial.println("RTC alarm wake detected - triggering event");

            doSomething();

            apModeActive = false;

            break;
        case ESP_SLEEP_WAKEUP_EXT1:
                // Serial.println("WAKEUP_EXT1");
            Serial.println("Button wake detected - starting AP mode");
            apModeActive = true;
            apStartTime = millis();
            break;
        case ESP_SLEEP_WAKEUP_TIMER:
            Serial.println("Timer wake");
            break;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        default:
            Serial.println("Not from deep sleep (first boot or reset)");
            // First boot - start in AP mode for initial configuration
            apModeActive = true;
            apStartTime = millis();
            break;
    }
    
    // If woken by RTC alarm (not button), trigger the event immediately
    if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("\nRTC alarm wake - triggering scheduled event...");
        
        DateTime now = rtc.now();
        uint32_t currentUnix = now.unixtime();
        
        doSomething();
        
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
            // Calculate new random time for next interval
            uint32_t intervalSeconds = (modeConfig.randIntervalHours * 3600UL + 
                                        modeConfig.randIntervalMinutes * 60UL);
            uint32_t randomOffset = random(0, intervalSeconds);
            modeConfig.randIntervalNextTriggerUnix = currentUnix + randomOffset;
            saveModeConfig();
            Serial.printf("New random trigger in %lu seconds\n", randomOffset);
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