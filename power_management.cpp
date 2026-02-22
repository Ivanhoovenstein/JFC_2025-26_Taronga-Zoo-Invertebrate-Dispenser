#include "power_management.h"
#include "storage.h"
#include "servo_control.h"
#include <WiFi.h>
#include <Wire.h>

// ========================================
// Power Management Functions
// ========================================

void enterDeepSleep() {
    Serial.println("\n========================================");
    Serial.println("Preparing to enter deep sleep...");
    
    // Save all data before sleeping
    saveAlarms();
    saveModeConfig();
    saveCompartmentPosition();
    
    // Detach servo
    if (myServo.attached()) {
        myServo.detach();
        Serial.println("Servo detached");
    }
    
    // Turn off WiFi
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);
    Serial.println("WiFi turned off");
    
    // Turn off I2C
    Wire.end();
    Serial.println("I2C stopped");
    
    // Set unused GPIOs to LOW
    const int unusedPins[] = {
        0, 2, 4, 5, 12, 13, 15,
        16, 17, 18, 19, 20, 21,
        22, 23
    };

    for (int pin : unusedPins) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }
    Serial.println("Unused GPIOs set low");

    // Turn off servo transistor
    digitalWrite(SERVO_TRANSISTOR_PIN, LOW);
    
    // Configure wake sources
    esp_sleep_enable_ext0_wakeup((gpio_num_t)RTC_ALARM_PIN, 0);
    esp_sleep_enable_ext1_wakeup(1ULL << BUTTON_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);
    
    // Disable other wake sources
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    
    Serial.println("\nWake sources configured:");
    Serial.printf("  - RTC Alarm on GPIO %d (active LOW)\n", RTC_ALARM_PIN);
    Serial.printf("  - Button on GPIO %d (active HIGH)\n", BUTTON_PIN);
    Serial.println("Entering deep sleep NOW...");
    Serial.println("========================================\n");
    
    delay(100);
    
    esp_deep_sleep_start();
}

bool shouldEnterSleep() {
    extern unsigned long apStartTime;
    extern bool apModeActive;
    
    if (apModeActive) {
        unsigned long elapsed = millis() - apStartTime;
        if (elapsed < AP_TIMEOUT_MS) {
            return false;
        } else {
            Serial.println("AP mode timeout reached");
            return true;
        }
    }
    
    return true;
}
