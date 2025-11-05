# Smart Health Watch - IoT Wearable Health Monitoring Device

A comprehensive wrist-worn health monitoring wearable that continuously tracks vital signs including heart rate, blood oxygen saturation (SpO₂), body temperature, and movement/activity patterns. The device syncs data via Bluetooth/Wi-Fi to a Flutter mobile app, which communicates with a FastAPI backend for real-time health analytics, trend predictions, and personalized health ratings.

## 🏗️ Project Architecture

```
smart-health-watch/
├── health-firmware/          # ESP32-S3 embedded firmware
│   ├── src/                  # Main firmware source code
│   ├── include/              # Header files
│   └── platformio.ini        # PlatformIO configuration
│
├── health_app/               # Flutter mobile application
│   ├── lib/                  # Dart source code
│   │   ├── main.dart         # App entry point
│   │   ├── models/           # Data models
│   │   ├── services/         # BLE, API services
│   │   ├── screens/          # UI screens
│   │   └── widgets/          # Reusable widgets
│   └── pubspec.yaml          # Dependencies
│
└── backend-api/              # FastAPI backend server
    ├── main.py               # FastAPI app entry point
    ├── models/               # Database models
    ├── routes/                # API routes
    ├── services/             # Business logic
    ├── ml/                   # Machine learning models
    └── pyproject.toml        # Python dependencies
```

## ✨ Key Features

### Hardware Capabilities
- **Real-time Heart Rate & SpO₂ Monitoring** - Continuous photoplethysmography (PPG) via MAX30102
- **Body Temperature Tracking** - Precise ±0.1°C accuracy with MAX30205
- **Activity Detection** - Steps, sleep tracking, fall detection via MPU6050 IMU
- **Haptic Feedback** - Vibration alerts for abnormal readings
- **OLED Display** - Quick stats display (128×64)
- **Low Power Operation** - 2-3 days battery life with smart sleep modes

### Mobile App Features
- **Real-time Dashboard** - Live vitals visualization with charts
- **Historical Trends** - 7-day/30-day health analytics
- **Smart Notifications** - Alerts for abnormal vitals
- **Sleep Analysis** - Deep/light/REM stage classification
- **Activity Recognition** - Walking, running, cycling detection
- **Health Ratings** - AI-powered cardiovascular and metabolic scores

### Backend Services
- **Real-time Data Ingestion** - RESTful API for vitals upload
- **Time-series Database** - PostgreSQL + TimescaleDB optimization
- **ML-powered Analytics** - Anomaly detection, sleep classification, stress prediction
- **Health Predictions** - Cardiovascular risk, sleep quality, stress levels
- **Personalized Recommendations** - AI-generated health insights

## 🚀 Quick Start

### Prerequisites
- **Hardware**: ESP32-S3-WROOM-1, MAX30102, MPU6050, MAX30205, OLED SSD1306
- **Software**: PlatformIO, Flutter 3.x, Python 3.11+, PostgreSQL 14+
- **Tools**: Arduino IDE (optional), VS Code, Git

### Installation

#### 1. Firmware Setup
```bash
cd health-firmware
# Install PlatformIO if not already installed
pip install platformio

# Build and upload to ESP32-S3
pio run -t upload
```

#### 2. Mobile App Setup
```bash
cd health_app
flutter pub get
flutter run
```

#### 3. Backend Setup
```bash
cd backend-api
pip install -e .
# Configure database (see backend-api/README.md)
uvicorn main:app --reload
```

## 📁 Component Documentation

- **[Firmware README](health-firmware/README.md)** - Embedded code architecture, sensor integration, BLE protocol
- **[Mobile App README](health_app/README.md)** - Flutter app structure, BLE connectivity, UI components
- **[Backend README](backend-api/README.md)** - API endpoints, database schema, ML models

## 🔧 Hardware Components

### Core Components
| Component | Quantity | Purpose |
|-----------|----------|---------|
| ESP32-S3-WROOM-1 | 1 | Main microcontroller (Wi-Fi/BLE 5.0) |
| MAX30102 | 1 | PPG sensor (heart rate, SpO₂) |
| MPU6050 | 1 | 6-axis IMU (accelerometer + gyroscope) |
| MAX30205 | 1 | Digital temperature sensor |
| SSD1306 OLED (128×64) | 1 | Display module |
| TP4056 Charger | 1 | Li-ion battery charging |
| Li-Po 3.7V 500mAh | 1 | Power supply |

### Circuit Components
- **Capacitors**: 0.1µF (decoupling), 1µF, 10µF, 22µF (filtering)
- **Resistors**: 10kΩ (I²C pull-ups), 1kΩ (LED limiting)
- **Diodes**: 1N5819 (reverse protection)
- **LEDs**: Red (charging), Green/Blue (status)

See [Hardware Component List](#hardware-components) for complete BOM.

## 📊 Power Consumption

| Mode | Current Draw | Battery Life (500mAh) |
|------|--------------|----------------------|
| Active monitoring | ~80mA | 6-7 hours |
| Display on | +15mA | 5 hours |
| Deep sleep | ~500µA avg | 40+ days |
| Typical use | ~12mA avg | 2-3 days |

## 🔌 Communication Protocols

### Bluetooth Low Energy (BLE)
- **Service UUID**: `12345678-1234-1234-1234-123456789abc`
- **Characteristics**:
  - `Heart Rate` (notify): Real-time HR values
  - `SpO₂` (notify): Blood oxygen saturation
  - `Temperature` (notify): Body temperature
  - `IMU Data` (notify): Accelerometer/gyroscope readings
  - `Battery Level` (read): Current battery percentage

### Wi-Fi Sync
- Periodic HTTP POST to backend API
- Automatic retry on connection failure
- Deep sleep between sync intervals

## 🧠 Machine Learning Models

1. **Anomaly Detection** (Isolation Forest)
   - Detects irregular heart rate patterns
   - Flags sudden SpO₂ drops

2. **Sleep Stage Classification** (Random Forest)
   - Wake/light/deep/REM classification from IMU data

3. **Stress Prediction** (LSTM)
   - HRV analysis for stress level estimation

4. **Activity Recognition** (CNN)
   - Walking, running, cycling, stationary detection

## 📈 API Endpoints

### Data Ingestion
- `POST /api/v1/vitals/upload` - Upload sensor data
- `GET /api/v1/vitals/latest/{user_id}` - Get latest 24h data

### Analytics
- `POST /api/v1/analytics/predict` - Get health predictions
- `GET /api/v1/ratings/{user_id}` - Get health ratings

See [Backend README](backend-api/README.md) for complete API documentation.


## 🔒 Security & Privacy

- **Data Encryption**: All BLE communications encrypted
- **Authentication**: JWT tokens for API access
- **Data Privacy**: Local storage with user consent
- **Compliance**: HIPAA-ready architecture (health data handling)

## 📝 License

This project is licensed under the MIT License - see the LICENSE file for details.

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## 📧 Contact

For questions or support, please open an issue on GitHub.

---

**Built with ❤️ for better health monitoring**


