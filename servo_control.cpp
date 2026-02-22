#include "servo_control.h"
#include "storage.h"

// ========================================
// Servo Control Functions
// ========================================

void moveToAngle(int angle) {
    float newAngle = angle;
    int pulse = map(angle, 0, MECH_RANGE, MIN_PULSE, MAX_PULSE);
    myServo.writeMicroseconds(pulse);
}

void advanceCompartment() {
    loadCompartmentPosition();

    Serial.println(compartment);
    int angle = (compartment + 1) * SERVO_ANGLE_STEP + SERVO_ANGLE_OFFSET;

    if (angle >= 300) {
        moveToAngle(angle);
        delay(SERVO_FINAL_DELAY);  // Allow last item to drop
        compartment = 0;

        moveToAngle(0);  // Return to deadspace
        saveCompartmentPosition();
        return;
    }

    moveToAngle(angle);

    compartment++;
    saveCompartmentPosition();
}

// ========================================
// Battery Monitoring Functions
// ========================================

float checkVoltage() {
    float busvoltage = ina219.getBusVoltage_V();
    return busvoltage; 
}

int voltageToSOC(float v) {
    // Piecewise approximation of the battery's charge
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
    Serial.print("Supply Voltage: "); 
    Serial.print(busvoltage); 
    Serial.println(" V");
    Serial.print("Battery Charge: "); 
    Serial.print(batteryPercent); 
    Serial.println(" %");

    return batteryPercent;
}
