# Smart Health Watch - Firmware Documentation

## Overview

ESP32-S3 firmware for the Smart Health Watch wearable device. This firmware manages all sensor readings, BLE communication, Wi-Fi sync, display updates, and power management.

## Hardware Components

### Sensors Integrated
- **MAX30102** - Heart rate & SpO₂ sensor (PPG)
- **MAX30205** - Digital temperature sensor (±0.1°C)
- **MPU6050** - 6-axis IMU (accelerometer + gyroscope)
- **SSD1306** - 128×64 OLED display

### Communication
- **Bluetooth Low Energy (BLE)** - Real-time data streaming to mobile app
- **Wi-Fi** - Periodic sync to FastAPI backend

## Pin Configuration

| Pin | Function | Description |
|-----|----------|-------------|
| GPIO21 | I2C SDA | I²C data line |
| GPIO22 | I2C SCL | I²C clock line |
| GPIO0 | Button 1 | Mode toggle |
| GPIO35 | Button 2 | Menu navigation |
| GPIO25 | Vibration Motor | Haptic feedback |
| GPIO26 | Charging LED | Charging indicator |
| GPIO27 | Status LED | BLE connection status |
| GPIO34 | Battery ADC | Battery voltage reading |

## Sensor Functions

### MAX30102 - Heart Rate & SpO₂

**Function**: `updateHeartRate()` and `updateSpO2()`

- Reads IR and Red LED photoplethysmography (PPG) signals
- Calculates heart rate using peak detection algorithm
- Estimates SpO₂ using red/IR ratio
- Sample rate: ~10Hz

**Key Features**:
- Automatic beat detection
- Moving average filtering (4-sample window)
- Valid range: 20-255 BPM

### MAX30205 - Temperature

**Function**: `updateTemperature()`

- Reads digital temperature via I²C
- Accuracy: ±0.1°C
- Range: 0°C to 50°C
- Sample rate: 1Hz

### MPU6050 - Motion Tracking

**Function**: `updateIMU()`

- 6-axis motion sensing (3-axis accel + 3-axis gyro)
- Accelerometer range: ±2G
- Gyroscope range: ±250°/s
- Sample rate: 50Hz

**Features**:
- Step counting via magnitude threshold
- Activity detection (walking, running, stationary)
- Fall detection capability

## BLE Protocol

### Service UUID
```
12345678-1234-1234-1234-123456789abc
```

### Characteristics

| Characteristic | UUID | Type | Data Format |
|---------------|------|------|-------------|
| Heart Rate | `00002a37-...` | Notify | uint8_t (BPM) |
| SpO₂ | `12345678-...789abd` | Notify | uint8_t (%) |
| Temperature | `12345678-...789abe` | Notify | int16_t (×100, °C) |
| IMU Data | `12345678-...789abf` | Notify | JSON string |
| Battery Level | `00002a19-...` | Notify | uint8_t (%) |

### IMU Data JSON Format
```json
{
  "ax": 0.5,
  "ay": -0.2,
  "az": 9.8,
  "gx": 0.01,
  "gy": 0.02,
  "gz": -0.01,
  "steps": 1234
}
```

## Power Management

### Operating Modes

1. **Active Mode** (~80mA)
   - All sensors active
   - BLE advertising/connected
   - Display on
   - Duration: 6-7 hours (500mAh battery)

2. **Deep Sleep Mode** (~500µA)
   - All peripherals off except RTC
   - Wake every 15 minutes for data sync
   - Duration: 40+ days

3. **Typical Use** (~12mA avg)
   - Mixed active/sleep periods
   - 2 hours active per day
   - Duration: 2-3 days

### Sleep Scheduling

```cpp
// Enter deep sleep after 15 minutes of inactivity
const unsigned long DEEP_SLEEP_INTERVAL = 15 * 60 * 1000;
```

## Display Functions

### `updateDisplay()`

Updates OLED display with current vitals:
- Heart Rate (BPM)
- SpO₂ (%)
- Temperature (°C)
- Step count
- Battery level (%)
- BLE connection status

**Update Rate**: 1 second

## Alert System

### Abnormal Reading Detection

```cpp
// High heart rate (>120 BPM) or low (<50 BPM)
if (vitals.heartRate > 120 || vitals.heartRate < 50) {
  vibrate(200);
}

// Low SpO₂ (<90%)
if (vitals.spo2 < 90) {
  vibrate(500);
}

// Abnormal temperature (>38°C or <35°C)
if (vitals.temperature > 38.0 || vitals.temperature < 35.0) {
  vibrate(300);
}
```

## Backend Sync

### Wi-Fi Configuration

Edit in `main.cpp`:
```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* backendUrl = "http://your-backend-api.com/api/v1/vitals/upload";
```

### Sync Interval

- Default: Every 15 minutes
- HTTP POST to backend API
- JSON payload format:
```json
{
  "user_id": "device_001",
  "timestamp": 1234567890,
  "heart_rate": 72.5,
  "spo2": 98,
  "temperature": 36.5,
  "steps": 5432,
  "accel_x": 0.1,
  "accel_y": -0.2,
  "accel_z": 9.8
}
```

## Building & Uploading

### Prerequisites
- PlatformIO installed
- ESP32-S3 development board
- USB cable

### Build Commands
```bash
# Install dependencies
pio lib install

# Build firmware
pio run

# Upload to device
pio run -t upload

# Monitor serial output
pio device monitor
```

### Configuration

Edit `platformio.ini` for different ESP32 variants:
```ini
[env:esp32-s3-devkitc-1]
board = esp32-s3-devkitc-1  # Change for different board
```

## Troubleshooting

### Sensor Not Detected
1. Check I²C wiring (SDA/SCL)
2. Verify sensor power (3.3V)
3. Check I²C address (use I²C scanner)
4. Ensure pull-up resistors (10kΩ) on SDA/SCL

### BLE Not Connecting
1. Check if device is advertising (scan with phone)
2. Verify service/characteristic UUIDs match app
3. Check BLE power (ensure not in deep sleep)
4. Restart ESP32

### Display Not Showing
1. Check I²C connection
2. Verify OLED address (usually 0x3C)
3. Check display contrast/brightness
4. Ensure display.init() succeeded

### High Power Consumption
1. Enable deep sleep mode
2. Reduce sensor sampling rates
3. Turn off display when not needed
4. Disable Wi-Fi when not syncing

## Code Structure

```
health-firmware/
├── src/
│   └── main.cpp          # Main firmware code
├── include/
│   ├── heartRate.h       # Heart rate detection algorithm
│   ├── MAX30205.h        # Temperature sensor driver
│   └── MAX30105.h        # PPG sensor wrapper
└── platformio.ini        # Build configuration
```

## Key Functions

| Function | Purpose |
|----------|---------|
| `initSensors()` | Initialize all sensors |
| `initDisplay()` | Initialize OLED display |
| `initBLE()` | Setup BLE server & characteristics |
| `updateHeartRate()` | Read and calculate heart rate |
| `updateSpO2()` | Calculate blood oxygen saturation |
| `updateTemperature()` | Read body temperature |
| `updateIMU()` | Read accelerometer/gyroscope |
| `updateDisplay()` | Refresh OLED display |
| `sendBLEData()` | Send vitals via BLE |
| `syncToBackend()` | HTTP POST to backend API |
| `readBatteryLevel()` | Read battery voltage |
| `vibrate()` | Trigger haptic feedback |
| `enterDeepSleep()` | Enter low-power mode |

## Future Enhancements

- [ ] Implement Kalman filtering for sensor data
- [ ] Add HRV (Heart Rate Variability) analysis
- [ ] Implement sleep stage classification
- [ ] Add gesture recognition (IMU-based)
- [ ] Implement OTA (Over-The-Air) updates
- [ ] Add data encryption for BLE
- [ ] Implement user authentication
- [ ] Add configurable alert thresholds

## License

MIT License - See main project README

