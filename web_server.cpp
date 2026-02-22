#include "web_server.h"
#include "storage.h"
#include "servo_control.h"
#include "alarm_manager.h"
#include "power_management.h"
#include <algorithm>

// ========================================
// CORS Headers
// ========================================

void setCORSHeaders() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, PATCH, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// ========================================
// Static File Serving
// ========================================

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

// ========================================
// Captive Portal Setup
// ========================================

void setupCaptivePortal() {
    WiFi.softAP(currentSSID.c_str(), NULL);
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

    Serial.print("AP running. Connect to: ");
    Serial.println(currentSSID.c_str());
    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());
}

// ========================================
// HTTP Route Registration
// ========================================

void registerRoutes() {

    // Captive Portal Detection
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

    // GET current servo position
    server.on("/api/servo", HTTP_GET, []() {
        setCORSHeaders();
        
        DynamicJsonDocument doc(JSON_BUFFER_SMALL);
        doc["compartment"] = compartment;
        doc["angle"] = compartment * SERVO_ANGLE_STEP;
        doc["maxCompartment"] = maxCompartment;
        
        String json;
        serializeJson(doc, json);
        server.send(200, "application/json", json);
    });

    // GET current battery charge
    server.on("/api/battery", HTTP_GET, []() {
        setCORSHeaders();

        int batteryPercent = runBatteryCheck();

        DynamicJsonDocument doc(JSON_BUFFER_SMALL);
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
        
        loadEventsFromFile();
        
        String json = eventsToJson();
        Serial.println("GET /api/events -> " + String(eventHistory.size()) + " events");
        server.send(200, "application/json", json);
    });

    server.on("/api/events", HTTP_OPTIONS, []() {
        setCORSHeaders();
        server.send(200, "text/plain", "");
    });
    
    // DELETE all events
    server.on("/api/events", HTTP_DELETE, []() {
        setCORSHeaders();
        
        eventHistory.clear();
        LittleFS.remove(FILE_EVENTS);
        
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
        
        DynamicJsonDocument doc(JSON_BUFFER_SMALL);
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

    // Handle OPTIONS for alarms
    server.on("/api/alarms/", HTTP_OPTIONS, []() {
        setCORSHeaders();
        server.send(200, "text/plain", "");
    });

    server.on("/api/settings", HTTP_OPTIONS, []() {
        setCORSHeaders();
        server.send(200, "text/plain", "");
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
        
        DynamicJsonDocument doc(JSON_BUFFER_SMALL);
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
        serveStaticFile(FILE_SETTINGS, "application/json");
    });

    // SETTINGS POST
    server.on("/api/settings", HTTP_POST, []() {
        setCORSHeaders();
        
        String body = server.arg("plain");
        Serial.println("POST /api/settings body: " + body);
        
        File f = LittleFS.open(FILE_SETTINGS, "w");
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
        triggerActivation(true);
        server.send(200, "text/plain", "OK");
    });

    server.on("/api/trigger-now", HTTP_OPTIONS, []() {
        setCORSHeaders();
        server.send(200, "text/plain", "");
    });

    // Reset Motor Position
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
        extern RTC_DS3231 rtc;
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
        extern RTC_DS3231 rtc;
        setCORSHeaders();
        
        DynamicJsonDocument doc(JSON_BUFFER_MEDIUM);
        doc["activeMode"] = modeConfig.activeMode;
        doc["regIntervalHours"] = modeConfig.regIntervalHours;
        doc["regIntervalMinutes"] = modeConfig.regIntervalMinutes;
        doc["randIntervalHours"] = modeConfig.randIntervalHours;
        doc["randIntervalMinutes"] = modeConfig.randIntervalMinutes;
        
        // Calculate next activation time
        DateTime now = rtc.now();
        uint32_t currentUnix = now.unixtime();
        String nextTime = "";
        
        if (modeConfig.activeMode == "set_times") {
            for (auto &a : alarms) {
                if (a.active) {
                    int alarmHour = a.time.substring(0, 2).toInt();
                    int alarmMin = a.time.substring(3, 5).toInt();
                    
                    if (alarmHour > now.hour() || 
                        (alarmHour == now.hour() && alarmMin > now.minute())) {
                        nextTime = a.time;
                        break;
                    }
                }
            }
            if (nextTime == "" && alarms.size() > 0) {
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
        extern RTC_DS3231 rtc;
        setCORSHeaders();
        
        String body = server.arg("plain");
        DynamicJsonDocument doc(JSON_BUFFER_SMALL);
        deserializeJson(doc, body);
        
        modeConfig.activeMode = "regular_interval";
        modeConfig.regIntervalHours = doc["hours"];
        modeConfig.regIntervalMinutes = doc["minutes"];
        
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
        DynamicJsonDocument doc(JSON_BUFFER_SMALL);
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
        
        delay(500);
        
        Serial.println("Manual sleep requested via API");
        server.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        digitalWrite(LED_PIN, LOW);
        
        delay(100);
        configureNextWake();
        enterDeepSleep();
    });

    // POST sync time from browser
    server.on("/api/sync-time", HTTP_POST, []() {
        extern RTC_DS3231 rtc;
        setCORSHeaders();
        
        String body = server.arg("plain");
        Serial.println("POST /api/sync-time body: " + body);
        
        DynamicJsonDocument doc(JSON_BUFFER_SMALL);
        DeserializationError error = deserializeJson(doc, body);
        
        if (error) {
            Serial.print("Error parsing sync-time JSON: ");
            Serial.println(error.c_str());
            logEvent("ERROR", "System", "Error parsing sync-time request");
            server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
            return;
        }
        
        long long timestampMs = doc["timestamp"];
        time_t epoch = timestampMs / 1000;
        DateTime newTime(epoch);
        
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
        
        DynamicJsonDocument doc(JSON_BUFFER_SMALL);
        doc["ssid"] = currentSSID;
        
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
        
        DynamicJsonDocument doc(JSON_BUFFER_SMALL);
        DeserializationError err = deserializeJson(doc, body);
        
        if (err) {
            Serial.print("Error parsing WiFi settings: ");
            Serial.println(err.c_str());
            logEvent("ERROR", "System", "Error parsing WiFi settings");
            server.send(400, "text/plain", "Invalid JSON");
            return;
        }
        
        String newSSID = doc["ssid"].as<String>();
        
        if (newSSID.length() == 0 || newSSID.length() > 32) {
            server.send(400, "application/json", 
                "{\"error\":\"SSID must be 1-32 characters\"}");
            return;
        }
        
        saveWiFiSettings(newSSID);
        currentSSID = newSSID;
        
        Serial.println("WiFi settings updated successfully");
        Serial.println("  New SSID: " + currentSSID);
        
        server.send(200, "application/json", 
            "{\"status\":\"ok\",\"message\":\"Settings saved. Changes will apply on next wake/restart.\"}");
    });

    // 404 fallback
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
        
        // Ignore favicon
        if (uri == "/favicon.ico") {
            server.send(204, "text/plain", "");
            return;
        }

        // Redirect captive portal detection
        if (uri.indexOf("generate_204") >= 0 || 
            uri.indexOf("gen_204") >= 0 ||
            uri.indexOf("ncsi") >= 0 ||
            uri.indexOf("connecttest") >= 0) {
            server.sendHeader("Location", "http://" + WiFi.softAPIP().toString());
            server.send(302, "text/plain", "");
            return;
        }
        
        // Default 404
        Serial.println("404: " + uri);
        server.sendHeader("Location", "http://" + WiFi.softAPIP().toString());
        server.send(302, "text/html", "");
    });
}
