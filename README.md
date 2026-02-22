# Taronga Zoo Programmable Invertebrate Dispenser Engineering and Product Development (Curlew Feeder) Setup and Usage Guide

An automated feeding system for Taronga Zoo Sydney, built on ESP32 with a web-based configuration interface.

---

## Table of Contents
1. [Features](#features)
2. [Arduino IDE Installation](#arduino-ide-installation)
3. [ESP32 Board Setup](#esp32-board-setup)
4. [Installing Required Libraries](#installing-required-libraries)
5. [Preparing Your Project Files](#preparing-your-project-files)
6. [Uploading the Sketch](#uploading-the-sketch)
7. [Uploading Web Files to LittleFS](#uploading-web-files-to-littlefs)
8. [Hardware Connections](#hardware-connections)
9. [API Endpoints](#api-endpoints)
10. [Usage](#usage)
11. [Customisation](#customisation)
12. [Troubleshooting](#troubleshooting)

---

## Features

- **Three Feeding Modes:**
  - Set Times: Schedule specific feeding times
  - Regular Intervals: Feed at consistent intervals
  - Random Intervals: Feed at random times within specified windows
  - (An additional manual activation option for staff use/testing)
  
- **Web Interface:**
  - Captive portal configuration
  - Real-time status monitoring
  - Event logging and history
  - Battery level monitoring
  
- **Power Management:**
  - Deep sleep mode for battery conservation
  - Wake on RTC alarm or button press
  - Automatic timeout after 15 minutes of inactivity

## Prerequisites

## Arduino IDE Installation

### Step 1: Download Arduino IDE

1. Go to https://www.arduino.cc/en/software
2. Download the installer for your operating system:
   - **Windows**: Download the `.exe` installer
   - **Mac**: Download the `.dmg` file
   - **Linux**: Download the `.AppImage` or use package manager

3. Run the installer and follow the prompts
4. Launch Arduino IDE after installation

### Step 2: Initial Setup

1. Open Arduino IDE
2. Go to **File → Preferences**
3. Note the "Sketchbook location" - this is where your projects will be saved
   - Windows: Usually `C:\Users\YourName\Documents\Arduino`
   - Mac: Usually `/Users/YourName/Documents/Arduino`
   - Linux: Usually `/home/YourName/Arduino`

---

## ESP32 Board Setup

### Step 1: Add ESP32 Board Support

1. In Arduino IDE, go to **File → Preferences**
2. Find "Additional Boards Manager URLs" field
3. Add this URL (if there are existing URLs, separate with commas):
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
4. Click **OK**

### Step 2: Install ESP32 Board Package

1. Go to **Tools → Board → Boards Manager**
2. In the search box, type "ESP32"
3. Find "esp32 by Espressif Systems"
4. Click **Install** (this may take several minutes)
5. Wait for installation to complete
6. Close the Boards Manager

### Step 3: Select Your ESP32 Board

1. Go to **Tools → Board → ESP32 Arduino**
2. Select your specific board:
   - Most common: **ESP32 Dev Module**
   - If you have a specific board (like DOIT ESP32 DevKit V1), select that

### Step 4: Configure Upload Settings

Go to **Tools** menu and set:
- **Upload Speed**: 921600 (or 115200 if you have issues)
- **Flash Frequency**: 80MHz
- **Flash Mode**: QIO
- **Flash Size**: 4MB (32Mb) - or match your board
- **Partition Scheme**: Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)
- **Core Debug Level**: None (or "Error" for troubleshooting)
- **PSRAM**: Disabled (unless your board has PSRAM)

---

## Installing Required Libraries

### Step 1: Open Library Manager

1. Go to **Tools → Manage Libraries** (or Sketch → Include Library → Manage Libraries)
2. The Library Manager window will open

### Step 2: Install Each Required Library

Search for and install the following libraries **one at a time**:

#### 1. ESP32Servo
- Search: "ESP32Servo"
- Author: **Kevin Harrington**
- Click **Install**

#### 2. RTClib
- Search: "RTClib"
- Author: **Adafruit**
- Click **Install**
- If prompted to install dependencies (like Adafruit BusIO), click **Install All**

#### 3. Adafruit INA219
- Search: "Adafruit INA219"
- Author: **Adafruit**
- Click **Install**
- If prompted to install dependencies, click **Install All**

#### 4. ArduinoJson
- Search: "ArduinoJson"
- Author: **Benoit Blanchon**
- Click **Install**
- **Important**: Install version 6.x (not version 7)

### Step 3: Verify Installation

After installing all libraries, you can verify by going to:
**Sketch → Include Library**

You should see all the installed libraries in the list.

---

## Preparing Your Project Files

### Step 1: Create Project Folder Structure

**CRITICAL**: Arduino IDE requires a specific folder structure.

1. Navigate to your Arduino sketchbook location (from File → Preferences)
2. Create a new folder called `feeder`
3. Inside the `feeder` folder, create these subfolders:
   ```
   feeder/
   └── (place all your files here - see below)
   ```

### Step 2: Organize Source Files

**All source files must be in the same folder as the .ino file.**

Copy these files into your `feeder/` folder:
```
feeder/
├── feeder.ino              ← Main sketch (MUST match folder name)
├── config.h
├── types.h
├── storage.h
├── storage.cpp
├── servo_control.h
├── servo_control.cpp
├── alarm_manager.h
├── alarm_manager.cpp
├── power_management.h
├── power_management.cpp
├── web_server.h
└── web_server.cpp
```

**Important Notes:**
- The `.ino` file name MUST match the folder name (`feeder.ino` in a folder named `feeder`)
- All `.h` and `.cpp` files must be in the same folder as the `.ino` file
- Arduino IDE will automatically compile all `.cpp` files in the sketch folder

### Step 3: Verify File Structure

Your complete path should look like:
```
C:\Users\YourName\Documents\Arduino\feeder\feeder.ino
```
or
```
/Users/YourName/Documents/Arduino/feeder/feeder.ino
```

---

## Uploading the Sketch

### Step 1: Open the Project

1. In Arduino IDE, go to **File → Open**
2. Navigate to your `feeder` folder
3. Select `feeder.ino`
4. Click **Open**

You should see multiple tabs at the top of the IDE window, one for each file in your project.

### Step 2: Connect Your ESP32

1. Connect your ESP32 to your computer via USB cable
2. Wait for your computer to recognise the device

### Step 3: Select the Correct Port

1. Go to **Tools → Port**
2. Select the port your ESP32 is connected to:
   - **Windows**: Usually `COM3`, `COM4`, etc.
   - **Mac**: Usually `/dev/cu.usbserial-xxxxx` or `/dev/cu.SLAB_USBtoUART`
   - **Linux**: Usually `/dev/ttyUSB0` or `/dev/ttyACM0`

### Step 4: Verify/Compile the Sketch

1. Click the **Verify** button at the top left
2. Wait for compilation to complete
3. Look for "Done compiling" in the status bar at the bottom

### Step 5: Upload to ESP32

1. Click the **Upload** button (→ arrow icon) next to the Verify button
2. The IDE will compile and upload the sketch
3. You'll see the upload progress in the bottom panel

---

## Uploading Web Files to LittleFS

Your web interface files (HTML, CSS, JS) need to be uploaded to the ESP32's filesystem.

### Step 1: Install LittleFS Upload Plugin

This article may be helpful in configuring the LittleFS file system with the ESP32: https://randomnerdtutorials.com/arduino-ide-2-install-esp32-littlefs/

#### For Arduino IDE 1.x:
1. Download the ESP32 LittleFS plugin from:
   https://github.com/lorol/arduino-esp32littlefs-plugin/releases
2. Download `ESP32FS-1.1.zip` (or latest version)
3. Extract the zip file
4. Copy the `ESP32FS` folder to your Arduino IDE's `tools` folder:
   - **Windows**: `C:\Users\YourName\Documents\Arduino\tools\ESP32FS\tool\esp32fs.jar`
   - **Mac**: `/Users/YourName/Documents/Arduino/tools/ESP32FS/tool/esp32fs.jar`
   - **Linux**: `/home/YourName/Arduino/tools/ESP32FS/tool/esp32fs.jar`
5. Create the `tools` folder if it doesn't exist
6. Restart Arduino IDE

#### For Arduino IDE 2.x:
The plugin installation is different. Instead:
1. Use the `arduino-littlefs-upload` tool from command line, OR
2. Use PlatformIO (see alternative below), OR
3. Continue using Arduino IDE 1.x for filesystem uploads

### Step 2: Create Data Folder

1. In your `feeder` project folder, create a new folder named `data`
2. Your structure should now look like:
   ```
   feeder/
   ├── feeder.ino
   ├── config.h
   ├── ... (all other source files)
   └── data/
       ├── index.html
       ├── script.js
       ├── style.css
       ├── taronga-zoo-logo.png
       ├── alarms.json
       ├── mode.json
       ├── servo.json
       ├── settings.json
       ├── wifi.json
       └── events.log
   ```

### Step 3: Prepare Configuration Files

Add the given files within the `data/` folder to your project (or create the following files in the `data/` folder):

#### `data/alarms.json`
```json
[]
```

#### `data/mode.json`
```json
{
  "activeMode": "set_times",
  "regIntervalHours": 0,
  "regIntervalMinutes": 30,
  "regIntervalLastTriggerUnix": 0,
  "randIntervalHours": 1,
  "randIntervalMinutes": 0,
  "randIntervalBlockStartUnix": 0,
  "randIntervalNextTriggerUnix": 0
}
```

#### `data/servo.json`
```json
{
  "compartment": 0,
  "angle": 0
}
```

#### `data/wifi.json`
```json
{
  "ssid": "Taronga Zoo Curlew Feeder"
}
```

#### `data/settings.json`
```json
{
  "timeFormat": "12",
  "theme": "light"
}
```

#### `data/events.log`
Just create an empty file (or leave it out - it will be created automatically)

### Step 4: Upload Filesystem

1. Close the Serial Monitor if it's open
2. In Arduino IDE 1.x: Go to **Tools → ESP32 Sketch Data Upload**
3. Wait for upload to complete (can take 1-2 minutes)
   
---

## Hardware Connections

### Wiring Diagram

Connect your components to the ESP32 according to these pin definitions:

```
ESP32 Pin → Component
─────────────────────────────────────────
GPIO 33   → SDA (DS3231 RTC, INA219)
GPIO 32   → SCL (DS3231 RTC, INA219)
GPIO 25   → SQW (DS3231 alarm output)
GPIO 34   → Push Button (other side to GND)
GPIO 14   → LED + (LED - to GND via 220Ω)
GPIO 13   → Transistor Base (controls servo power)
GPIO 12   → Servo Signal Pin

3.3V      → VCC (DS3231, INA219)
GND       → GND (All components)

Servo Power → Transistor Collector → Battery/Power
Servo GND   → GND
```

### Detailed Connection Steps

#### 1. I2C Components (RTC and Battery Sensor)

**DS3231 RTC Module:**
```
DS3231    →  ESP32
VCC       →  3.3V
GND       →  GND
SDA       →  GPIO 33
SCL       →  GPIO 32
SQW       →  GPIO 25
```
- Install CR2032 battery in RTC module
- **IMPORTANT**: Ensure that the DS3231 201 resistor is removed if using a CR2032 battery (which is non-rechargeable). This will break the in-built charging circuit and prevent damage to the battery.

**INA219 Current Sensor:**
```
INA219    →  ESP32
VCC       →  3.3V
GND       →  GND
SDA       →  GPIO 33 (same as RTC)
SCL       →  GPIO 32 (same as RTC)
VIN+      →  Positive battery terminal
VIN-      →  Negative battery terminal (through load)
```

#### 2. Push Button (Wake Button)
```
Button Pin 1  →  GPIO 34
Button Pin 2  →  GND
```
- Add 10kΩ pull-up resistor from GPIO 34 to 3.3V (optional)

#### 3. LED Indicator
```
LED Anode (+)  →  GPIO 14
LED Cathode (-) →  220Ω Resistor →  GND
```

#### 4. Servo Control Circuit

**Important**: The servo draws more current than the ESP32 can provide, so we use a transistor to control servo power.

```
GPIO 13  →  1kΩ Resistor  →  Transistor Base
Transistor Emitter  →  GND
Transistor Collector  →  Servo VCC (Power)
Battery Positive  →  Servo Power Rail
GPIO 12  →  Servo Signal Pin
GND  →  Servo GND
```

---

## API Endpoints

### Alarms
- `GET /api/alarms` - Get all alarms
- `POST /api/alarms` - Add new alarm
- `PATCH /api/alarms/{id}` - Toggle alarm on/off
- `DELETE /api/alarms/{id}` - Delete alarm

### Mode
- `GET /api/mode` - Get current mode configuration
- `POST /api/mode/set-times` - Set mode to scheduled times
- `POST /api/mode/regular-interval` - Set regular interval mode
- `POST /api/mode/random-interval` - Set random interval mode

### System
- `GET /api/time` - Get RTC time
- `POST /api/sync-time` - Sync RTC with device time
- `GET /api/battery` - Get battery level
- `GET /api/servo` - Get servo position
- `POST /api/reset-motor` - Reset servo to position 0
- `POST /api/trigger-now` - Manual feeding trigger
- `POST /api/sleep` - Enter sleep mode

### Events
- `GET /api/events` - Get event history
- `GET /api/events/stats` - Get event statistics
- `DELETE /api/events` - Clear event history

### WiFi
- `GET /api/wifi` - Get WiFi settings
- `POST /api/wifi` - Update WiFi SSID

## Usage

### First Time Setup

1. Power on the device
2. The device creates a WiFi access point: "Taronga Zoo Curlew Feeder"
3. Connect to this network with your phone/laptop
4. A captive portal should automatically open
5. If not, navigate to: `192.168.4.1`
6. Configure your feeding schedule
7. The device will sleep after 15 minutes of inactivity or by manually setting the device to sleep (in settings)

### Wake Device

- **Button Press:** Press the wake button to start configuration mode
- **Scheduled Wake:** Device automatically wakes at scheduled feeding times. The device will go back to sleep after it has completed its scheduled activation.

### Web Interface

The web interface provides:

- **Dashboard:** View current status and next feeding time
- **Schedule:** Configure feeding times and modes
- **Events:** View feeding history and system events
- **Settings:** Adjust WiFi name, time sync, and preferences
- **Manual Feed:** Trigger immediate feeding
- **Sleep:** Manually put device to sleep

## Customisation

### Change Feeding Compartments

Edit `config.h`:
```cpp
#define MAX_COMPARTMENTS 6  // Change to your number of compartments
```

### Change AP Timeout

Edit `config.h`:
```cpp
#define AP_TIMEOUT_MS 900000UL  // 15 minutes (in milliseconds)
```

### Adjust Servo Angles

Edit `config.h`:
```cpp
#define SERVO_ANGLE_STEP 60      // Degrees per compartment
#define SERVO_ANGLE_OFFSET 5     // Fine-tune alignment
```

### Battery Voltage Mapping

Edit `voltageToSOC()` in `servo_control.cpp` to match your battery characteristics.

## Troubleshooting

### Device won't wake from sleep
- Check battery level
- Verify RTC has backup battery
- Check button wiring

### Web interface not accessible
- Ensure you're connected to the device's WiFi
- Try `192.168.4.1` directly
- Check LED indicator (should be ON in AP mode)

### Servo not moving
- Check transistor control pin
- Verify servo power connection
- Check battery voltage

### Time keeps resetting
- Replace RTC backup battery (CR2032)
- Sync time via web interface after each power cycle

---

## Acknowledgments

Built for Taronga Zoo's conservation efforts with the Bush Stone-curlew (Her name is Uma).

