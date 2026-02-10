# Health Monitoring System - ESP32 Firmware

## Overview
Complete firmware for multi-vitals health monitoring with heart rate, SpO2, and temperature tracking.

## Features
- ✅ **Heart Rate Monitoring** - Sen-11574 PPG sensor with peak detection (30-200 BPM)
- ✅ **SpO2 Monitoring** - Blood oxygen saturation from Sen-11574
- ✅ **Intelligent Temperature Failover** - DS18B20 primary, Liebermeister's Rule fallback
- ✅ **20x4 LCD Display** - 4 rotating screens (vitals, sensor status, alerts, summary)
- ✅ **WiFi Cloud Sync** - Sends data to `/health/vitals` every 5 seconds
- ✅ **Critical Alert System** - Rapid LED blink for SpO2 < 90%
- ✅ **Battery Monitoring** - Voltage divider on GPIO 35

## Hardware Requirements

### Components
1. ESP32 DevKit V1
2. Sen-11574 PPG Sensor (analog heart rate + SpO2)
3. DS18B20 Temperature Sensor (OneWire)
4. 20x4 I2C LCD Display
5. 3.7V LiPo Battery + TP4056 Charger
6. LED (red for alerts)
7. 4.7kΩ resistor (OneWire pull-up)
8. 2x 10kΩ resistors (battery voltage divider)

### Wiring Diagram

```
ESP32 GPIO 21 -------- LCD SDA
ESP32 GPIO 22 -------- LCD SCL
ESP32 GPIO 4 --------- DS18B20 Data (+ 4.7kΩ pull-up to 3.3V)
ESP32 GPIO 34 -------- Sen-11574 Signal
ESP32 GPIO 35 -------- Battery Voltage (via divider)
ESP32 GPIO 2 --------- Built-in LED (status)
ESP32 GPIO 5 --------- External Red LED (alerts)

Sen-11574:
  VIN (+) -------- 3.3V
  GND (-) -------- GND
  Signal (S) ----- GPIO 34

DS18B20:
  VDD (Red) ------ 3.3V
  GND (Black) ---- GND
  Data (Yellow) -- GPIO 4
  4.7kΩ Resistor -- Between Data and VDD

LCD 20x4 I2C:
  VCC ------------ 5V (or 3.3V depending on module)
  GND ------------ GND
  SDA ------------ GPIO 21
  SCL ------------ GPIO 22

Battery Monitor:
  Battery+ ------- 10kΩ ------- GPIO 35 ------- 10kΩ ------- GND
```

## Configuration

### 1. WiFi Credentials
Edit `include/config.h`:
```cpp
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
```

### 2. Backend API
Edit `include/config.h`:
```cpp
#define API_BASE_URL "http://192.168.1.100:8000"
```

### 3. LCD I2C Address
If LCD doesn't display, try changing address in `config.h`:
```cpp
#define LCD_I2C_ADDRESS 0x3F  // Change from 0x27 if needed
```

## Building & Uploading

### PlatformIO
```bash
cd health-firmware
pio run -t upload -t monitor
```

### Arduino IDE
1. Install libraries:
   - OneWire
   - DallasTemperature
   - LiquidCrystal_I2C
   - ArduinoJson
2. Select board: ESP32 Dev Module
3. Upload

## LCD Screens

### Screen 1: Live Vitals (Main)
```
HR:72 BPM  O2:98%
Temp: 36.8C
Batt: 78%    WiFi:Y
Status: Normal
```

### Screen 2: Sensor Status
```
Sensor Status:
HR:  Good (95%)
SpO2:Good (90%)
Temp: DS18B20
```

### Screen 3: Alerts
```
CRITICAL ALERT!
Low SpO2: 89%
SEEK MEDICAL HELP!
[Auto-rotate 10s]
```

### Screen 4: Summary
```
Current Stats:
HR: 72 BPM
SpO2: 98%
Temp: 36.8C
```

## Temperature Failover Logic

### Normal Operation (DS18B20 Working)
- Reads DS18B20 every 10 seconds
- Displays "DS18B20" as source
- Temperature accurate to ±0.5°C

### Failover Triggered When:
- DS18B20 returns -127°C (disconnected)
- Reading out of range (< 30°C or > 45°C)
- No response after 3 attempts

### Fallback Mode (Liebermeister's Rule)
- Formula: `Temp = 36.5 + ((Current_HR - Resting_HR) / 10)`
- Displays "(Est)" on LCD
- Source shows "ESTIMATED"
- **Warning**: Only accurate at rest - exercise inflates estimate

### Auto-Recovery
- Continues trying DS18B20 every 10 seconds
- Automatically switches back when sensor reconnects
- User notified via LCD display change

## Alert Levels

### CRITICAL (Rapid LED Blink - 250ms)
- SpO2 < 90%
- Hypothermia (< 35.5°C)

### WARNING (Slow LED Blink - 500ms)
- SpO2 90-94%
- High HR (> 100 BPM)
- Low HR (< 50 BPM)
- Fever (> 38°C)

### INFO
- Temperature estimated
- Low battery (< 20%)

## Serial Monitor Output

```
===== Multi-Vitals Health Monitor =====
Device ID: ESP32_A4CF12EF3B2C
Backend: http://192.168.1.100:8000/health/vitals
========================================
WiFi connected
IP: 192.168.1.150
✓ Data sent to /health/vitals
✓ Data sent to /health/vitals
```

## Calibration

### Resting Heart Rate
For accurate temperature estimation, calibrate resting HR:
1. Sit still for 5 minutes
2. Note average HR from LCD
3. Use mobile app or backend to set resting HR
4. ESP32 stores in Preferences (persists across reboots)

## Troubleshooting

### LCD Not Displaying
- Check I2C address (0x27 or 0x3F)
- Run I2C scanner to find address
- Verify SDA/SCL connections

### No Heart Rate Reading
- Ensure Sen-11574 is properly connected to GPIO 34
- Place finger firmly on sensor
- Check signal quality on Screen 2

### Temperature Always Estimated
- Check DS18B20 connections
- Verify 4.7kΩ pull-up resistor
- Try different DS18B20 sensor

### WiFi Won't Connect
- Verify SSID/password in config.h
- Check router allows ESP32 connections
- Try moving closer to router

### Cloud Sync Failing
- Verify backend is running
- Check API_BASE_URL in config.h
- Ensure backend has `/health/vitals` endpoint
- Check firewall settings

## Data Flow

```
Sen-11574 (500Hz) → PulseSensor Class → HR + SpO2 Values
                                              ↓
DS18B20 (10s) → TemperatureSensor Class → Temp Value (or estimate)
                                              ↓
                        Update currentVitals Structure
                                   ↓
                   ┌──────────┬────────┬──────────┐
                   ↓          ↓        ↓          ↓
                  LCD      Alerts    LEDs    Cloud Sync
               (500ms)   (1000ms)  (Instant)  (5000ms)
```

## Power Consumption

- **Active WiFi**: ~160mA
- **LCD Backlight**: ~20mA
- **Sensors**: ~10mA
- **Total**: ~190mA

**Battery Life Estimate** (2000mAh LiPo):
- ~10 hours continuous operation
- Implement deep sleep for longer life

## Next Steps
1. Flash firmware to ESP32
2. Configure WiFi and API settings
3. Start backend server
4. Place finger on Sen-11574
5. Observe vitals on LCD
6. Check backend receives data

## Safety Notice
⚠️ **This is a prototype educational project**
- Not FDA approved
- Not for medical diagnosis
- SpO2 readings are estimates
- Seek professional medical attention for health concerns
