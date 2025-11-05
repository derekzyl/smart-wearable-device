# Smart Health Watch - Component Functions Summary

This document provides a brief overview of each component and its primary functions.

## Hardware Components

### ESP32-S3-WROOM-1
**Function**: Main microcontroller
- Dual-core processor (240MHz)
- Wi-Fi 802.11 b/g/n
- Bluetooth Low Energy 5.0
- Low-power modes for battery optimization
- Handles all sensor data collection and communication

### MAX30102 PPG Sensor
**Function**: Heart rate and SpO₂ measurement
- Photoplethysmography (PPG) using red and IR LEDs
- Measures heart rate via pulse detection
- Calculates blood oxygen saturation (SpO₂)
- Sample rate: ~10Hz
- I²C interface

### MAX30205 Temperature Sensor
**Function**: Body temperature measurement
- Digital temperature sensor
- Accuracy: ±0.1°C
- Range: 0°C to 50°C
- I²C interface
- Fast response time

### MPU6050 IMU
**Function**: Motion and orientation tracking
- 6-axis motion sensing (3-axis accelerometer + 3-axis gyroscope)
- Step counting via magnitude threshold detection
- Activity recognition (walking, running, stationary)
- Fall detection capability
- Sleep posture analysis
- Sample rate: 50Hz

### SSD1306 OLED Display
**Function**: Local vitals display
- 128×64 pixel monochrome display
- Shows real-time heart rate, SpO₂, temperature, steps, battery
- I²C interface
- Low power consumption

### TP4056 Charger Module
**Function**: Battery charging and protection
- 1A charging current
- Over-charge/discharge protection
- USB-C or micro-USB input
- Charging status LED

### Li-Po Battery (3.7V 500mAh)
**Function**: Power supply
- Rechargeable lithium polymer battery
- Typical runtime: 2-3 days (mixed use)
- Deep sleep mode: 40+ days

## Firmware Components

### main.cpp
**Primary Functions**:
- `initSensors()` - Initialize all sensors
- `initDisplay()` - Setup OLED display
- `initBLE()` - Configure Bluetooth Low Energy
- `updateHeartRate()` - Read and calculate heart rate
- `updateSpO2()` - Calculate blood oxygen saturation
- `updateTemperature()` - Read body temperature
- `updateIMU()` - Read accelerometer/gyroscope data
- `updateDisplay()` - Refresh OLED with current vitals
- `sendBLEData()` - Stream data via BLE
- `syncToBackend()` - HTTP POST to backend API
- `readBatteryLevel()` - Monitor battery voltage
- `vibrate()` - Haptic feedback alerts

### heartRate.h
**Function**: Heart rate detection algorithm
- Peak detection algorithm
- Beat-to-beat interval calculation
- Moving average filtering
- Validates heart rate range (20-255 BPM)

### MAX30205.h
**Function**: Temperature sensor driver
- I²C communication
- Temperature reading and conversion
- Error handling

## Mobile App Components

### main.dart
**Function**: App entry point
- Initializes database
- Sets up Provider state management
- Configures app theme and routing

### BLEService
**Primary Functions**:
- `checkBluetooth()` - Verify Bluetooth availability
- `startScan()` - Scan for BLE devices
- `connect()` - Connect to health watch device
- `disconnect()` - Disconnect from device
- Receives real-time vitals data via BLE notifications
- Manages connection state

### ApiService
**Primary Functions**:
- `uploadVitals()` - Upload data to backend
- `getLatestVitals()` - Fetch recent vitals data
- `getHealthPredictions()` - Get ML predictions
- `getHealthRatings()` - Fetch health ratings
- Handles authentication tokens

### DatabaseService
**Primary Functions**:
- `init()` - Initialize SQLite database
- `insertVitals()` - Store vitals locally
- `getVitals()` - Query historical data
- `deleteOldVitals()` - Cleanup old data

### DashboardScreen
**Function**: Main UI screen
- Displays real-time vitals in cards
- Shows trend charts
- Battery level indicator
- Connection status

### DeviceScanScreen
**Function**: BLE device discovery
- Scans for available devices
- Lists discovered devices
- Handles device connection

## Backend Components

### main.py
**Function**: FastAPI application
- Application initialization
- CORS configuration
- Route registration
- Lifespan management

### database.py
**Function**: Database configuration
- Async database engine setup
- Session factory
- Database initialization

### models.py
**Database Models**:
- `User` - User accounts and profiles
- `VitalsData` - Time-series health data
- `HealthRating` - Calculated health ratings

### routes/vitals.py
**Endpoints**:
- `POST /upload` - Upload vitals data
- `GET /latest/{user_id}` - Get recent vitals
- `GET /stats/{user_id}` - Get statistics

### routes/analytics.py
**Endpoints**:
- `POST /predict` - Get health predictions

### routes/ratings.py
**Endpoints**:
- `GET /{user_id}` - Get health ratings

### routes/auth.py
**Endpoints**:
- `POST /register` - User registration
- `POST /login` - User authentication

### services/ml_service.py
**ML Functions**:
- `predict_health()` - Main prediction function
- `_detect_anomalies()` - Isolation Forest anomaly detection
- `_calculate_risk_score()` - Overall health risk (0-100)
- `_predict_sleep_quality()` - Sleep quality score
- `_predict_stress_level()` - Stress classification
- `_predict_cardiovascular_risk()` - CV risk assessment
- `_generate_recommendations()` - Personalized recommendations

### services/rating_service.py
**Rating Functions**:
- `calculate_ratings()` - Main rating calculation
- `_calculate_cardiovascular_health()` - CV health score (0-100)
- `_calculate_respiratory_efficiency()` - Respiratory score (0-100)
- `_determine_metabolic_rate()` - Metabolic rate category
- `_calculate_fitness_age()` - Fitness age estimation

## Data Flow

1. **Sensor Collection** (Firmware)
   - ESP32 reads sensors continuously
   - Data filtered and processed locally
   - Stored temporarily in memory

2. **BLE Transmission** (Firmware → App)
   - Real-time streaming via BLE characteristics
   - App receives notifications
   - Data displayed immediately on dashboard

3. **Local Storage** (App)
   - SQLite database stores all received data
   - Enables offline access
   - Historical trend analysis

4. **Backend Sync** (App → Backend)
   - Periodic HTTP POST to backend API
   - Batch upload of historical data
   - Automatic retry on failure

5. **Analytics & ML** (Backend)
   - Time-series data stored in PostgreSQL
   - ML models analyze patterns
   - Generate predictions and recommendations

6. **User Feedback** (Backend → App)
   - Health ratings displayed
   - Predictions shown to user
   - Recommendations provided

## Communication Protocols

### BLE GATT
- Service UUID: `12345678-1234-1234-1234-123456789abc`
- Characteristics for each vitals type
- Notification-based updates

### HTTP REST
- JSON payload format
- JWT authentication
- Standard HTTP methods (GET, POST)

### I²C
- Sensor communication
- 400kHz fast mode
- Standard I²C addressing

## Power Management

### Active Mode
- All sensors active
- BLE connected
- Display on
- Current: ~80mA

### Sleep Mode
- Sensors in low-power
- BLE advertising only
- Display off
- Current: ~10mA

### Deep Sleep Mode
- All peripherals off
- RTC wake-up only
- Current: ~500µA

## Error Handling

### Firmware
- Sensor initialization checks
- Connection retry logic
- Data validation before transmission

### App
- BLE connection error handling
- Network retry with exponential backoff
- Local cache fallback

### Backend
- Input validation via Pydantic
- Database transaction rollback
- ML model error handling

---




