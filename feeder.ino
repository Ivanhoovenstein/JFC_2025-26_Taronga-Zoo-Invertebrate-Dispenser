// ========================================
// Taronga Zoo Curlew Feeder
// ========================================

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

// Project headers
#include "config.h"
#include "types.h"
#include "storage.h"
#include "servo_control.h"
#include "alarm_manager.h"
#include "power_management.h"
#include "web_server.h"

// ========================================
// Global Variable Definitions
// ========================================

// Hardware objects
Servo myServo;
Adafruit_INA219 ina219;
RTC_DS3231 rtc;
WebServer server(80);
DNSServer dnsServer;

// Data structures
std::vector<EventLog> eventHistory;
std::vector<Alarm> alarms;
ModeConfig modeConfig;

// State variables
int compartment = 0;
int maxCompartment = MAX_COMPARTMENTS;
unsigned long apStartTime = 0;
bool apModeActive = false;
String currentSSID = DEFAULT_SSID;

// ========================================
// Setup
// ========================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n=== ESP32 Alarm System Starting ===");
    
    // Configure pins
    pinMode(RTC_ALARM_PIN, INPUT_PULLUP);
    pinMode(BUTTON_PIN, INPUT);
    pinMode(LED_PIN, OUTPUT);
    pinMode(SERVO_TRANSISTOR_PIN, OUTPUT);

    // Turn on servo transistor
    digitalWrite(SERVO_TRANSISTOR_PIN, HIGH);

    // Initialize servo
    myServo.attach(SERVO_PIN);
    myServo.setPeriodHertz(50);

    // Start file system
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed");
        logEvent("ERROR", "System", "Flash Memory (LittleFS) error on startup");
        return;
    }
    Serial.println("LittleFS mounted");
    
    // Check wake reason
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    // Initialize I2C
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    // Initialize RTC
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

    // Initialize battery sensor
    if (!ina219.begin()) {
        Serial.println("Failed to find INA219 chip");
        logEvent("ERROR", "System", "Failed to find INA219 (battery sensor) on startup");
    } else {
        Serial.println("INA219 (Battery Sensor) Found");
        runBatteryCheck();
    }

    // Load configuration from storage
    loadCompartmentPosition();
    loadWiFiSettings();
    initSettings();
    alarms.clear();
    loadAlarms();
    loadModeConfig();
    loadEventsFromFile();
    
    // Handle wake reason
    Serial.print("Wake reason: ");
    switch(wakeup_reason) {
        case ESP_SLEEP_WAKEUP_EXT0:
            Serial.println("RTC Alarm");
            apModeActive = false;
            break;
            
        case ESP_SLEEP_WAKEUP_EXT1:
            Serial.println("Button wake detected - starting AP mode");
            delay(300);
            
            // If button is still low after delay, false alarm -> go back to sleep
            if (digitalRead(BUTTON_PIN) == 0) {
                configureNextWake();
                enterDeepSleep();
            }

            logEvent("SUCCESS", "System", "System started/woke from sleep");
            apModeActive = true;
            apStartTime = millis();
            break;
            
        case ESP_SLEEP_WAKEUP_TIMER:
            Serial.println("Timer wake");
            break;
            
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        default:
            Serial.println("Not from deep sleep (first boot or reset)");
            logEvent("SUCCESS", "System", "Initial system start");
            apModeActive = true;
            apStartTime = millis();
            break;
    }
    
    // If woken by RTC alarm, trigger event and go back to sleep
    if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("\nRTC alarm wake - triggering scheduled event...");

        DateTime now = rtc.now();
        uint32_t currentUnix = now.unixtime();
        
        triggerActivation();
        
        // Update trigger times based on mode
        if (modeConfig.activeMode == "set_times") {
            Serial.println("Set times mode - no update needed");
        }
        else if (modeConfig.activeMode == "regular_interval") {
            modeConfig.regIntervalLastTriggerUnix = currentUnix;
            saveModeConfig();
            Serial.printf("Updated last trigger to: %lu\n", currentUnix);
        }
        else if (modeConfig.activeMode == "random_interval") {
            calculateNextRandomInterval();
        }
        
        // Go back to sleep immediately after triggering
        configureNextWake();
        delay(1000);
        enterDeepSleep();
    }
    
    // Start AP mode if needed
    if (apModeActive) {
        setupCaptivePortal();
        registerRoutes();
        server.begin();
        digitalWrite(LED_PIN, HIGH);
        
        Serial.println("Web server started.");
        Serial.printf("AP mode will timeout in %lu minutes\n", AP_TIMEOUT_MS / 60000);
    }
    
    Serial.println("================================");
}

// ========================================
// Loop
// ========================================
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
            digitalWrite(LED_PIN, LOW);
            
            delay(100);
            
            // Configure next wake and sleep
            configureNextWake();
            enterDeepSleep();
        }
        
        // Show countdown every 60 seconds
        static unsigned long lastCountdown = 0;
        if (millis() - lastCountdown >= COUNTDOWN_INTERVAL) {
            unsigned long remaining = AP_TIMEOUT_MS - (millis() - apStartTime);
            Serial.printf("AP mode time remaining: %lu minutes\n", remaining / 60000);
            lastCountdown = millis();
        }
    } else {
        // Should never reach here in normal operation
        delay(100);
        configureNextWake();
        enterDeepSleep();
    }
    
    delay(100);
}
