#ifndef CONFIG_H
#define CONFIG_H

// ========================================
// Pin Definitions
// ========================================
#define I2C_SDA_PIN 33
#define I2C_SCL_PIN 32
#define RTC_ALARM_PIN 25    // DS3231 SQW pin connected 
#define BUTTON_PIN 34       // Button for manual wake
#define LED_PIN 14
#define SERVO_TRANSISTOR_PIN 13
#define SERVO_PIN 12

// ========================================
// Servo Configuration
// ========================================
#define MECH_RANGE 360.0
#define MAX_COMPARTMENTS 6
#define MIN_PULSE 500
#define MAX_PULSE 2500
#define SERVO_ANGLE_OFFSET 5
#define SERVO_ANGLE_STEP 60
#define SERVO_FINAL_DELAY 2000  // Delay before returning to deadspace (ms)

// ========================================
// Timing Constants
// ========================================
#define AP_TIMEOUT_MS 900000UL  // 15 minutes in milliseconds
#define MAX_EVENTS_IN_MEMORY 100
#define EVENT_RETENTION_SECONDS 86400  // 24 hours
#define TRIGGER_CHECK_INTERVAL 1000    // Check triggers every 1 second
#define COUNTDOWN_INTERVAL 60000       // Show AP countdown every 60 seconds

// ========================================
// WiFi Configuration
// ========================================
#define DEFAULT_SSID "Taronga Zoo Curlew Feeder"
#define DNS_PORT 53

// ========================================
// File Paths
// ========================================
#define FILE_ALARMS "/alarms.json"
#define FILE_MODE "/mode.json"
#define FILE_SERVO "/servo.json"
#define FILE_WIFI "/wifi.json"
#define FILE_SETTINGS "/settings.json"
#define FILE_EVENTS "/events.log"

// ========================================
// JSON Buffer Sizes
// ========================================
#define JSON_BUFFER_SMALL 256
#define JSON_BUFFER_MEDIUM 512
#define JSON_BUFFER_LARGE 4096
#define JSON_BUFFER_XLARGE 8192

#endif // CONFIG_H
