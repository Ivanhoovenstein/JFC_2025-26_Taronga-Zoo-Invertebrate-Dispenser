#ifndef SERVO_CONTROL_H
#define SERVO_CONTROL_H

#include <Arduino.h>
#include <ESP32Servo.h>
#include <Adafruit_INA219.h>
#include "config.h"

// ========================================
// Servo Control Functions
// ========================================

void moveToAngle(int angle);
void advanceCompartment();

// ========================================
// Battery Monitoring Functions
// ========================================

float checkVoltage();
int voltageToSOC(float v);
int runBatteryCheck();

// ========================================
// Global Hardware Objects (extern)
// ========================================

extern Servo myServo;
extern Adafruit_INA219 ina219;

#endif // SERVO_CONTROL_H
