# Smart Health Watch - Flutter Mobile App

## Overview

Flutter mobile application for the Smart Health Watch wearable device. The app connects to the ESP32-S3 device via Bluetooth Low Energy (BLE) to receive real-time health data, displays it in a beautiful dashboard, and syncs with the FastAPI backend for analytics and predictions.

## Features

### Core Functionality
- **BLE Device Scanning** - Automatic discovery and connection to health watch
- **Real-time Vitals Display** - Live heart rate, SpO₂, temperature, and steps
- **Historical Charts** - Trend visualization for all metrics
- **Local Data Storage** - SQLite database for offline access
- **Backend Sync** - Automatic upload to FastAPI backend
- **Health Predictions** - AI-powered insights and recommendations

### User Interface
- **Dashboard Screen** - Main vitals display with cards and charts
- **Device Scan Screen** - BLE device discovery and pairing
- **Trends Screen** - Historical data visualization (7-day/30-day)
- **Settings Screen** - User profile, alerts, preferences

## Project Structure

```
health_app/
├── lib/
│   ├── main.dart                 # App entry point
│   ├── models/                   # Data models
│   │   └── vitals_data.dart     # Vitals and IMU data models
│   ├── services/                 # Business logic
│   │   ├── ble_service.dart      # BLE connectivity
│   │   ├── api_service.dart     # Backend API calls
│   │   └── database_service.dart # Local SQLite database
│   ├── screens/                  # UI screens
│   │   ├── dashboard_screen.dart # Main dashboard
│   │   └── device_scan_screen.dart # BLE scanning
│   └── widgets/                  # Reusable widgets
│       ├── vitals_card.dart     # Vitals display card
│       └── chart_widget.dart    # Chart visualization
├── pubspec.yaml                  # Dependencies
└── README.md                     # This file
```

## Dependencies

### Core Packages
- `flutter_blue_plus: ^1.14.0` - BLE connectivity
- `http: ^1.1.0` - HTTP requests to backend
- `fl_chart: ^0.65.0` - Beautiful charts
- `provider: ^6.1.0` - State management
- `sqflite: ^2.3.0` - Local SQLite database
- `shared_preferences: ^2.2.0` - Local key-value storage
- `intl: ^0.18.0` - Date/time formatting
- `path_provider: ^2.1.0` - File system paths

## BLE Protocol

### Service UUID
```
12345678-1234-1234-1234-123456789abc
```

### Characteristics

| Characteristic | UUID | Data Type | Description |
|---------------|------|-----------|-------------|
| Heart Rate | `00002a37-...` | uint8_t | Beats per minute |
| SpO₂ | `12345678-...789abd` | uint8_t | Blood oxygen % |
| Temperature | `12345678-...789abe` | int16_t | Temperature ×100 |
| IMU Data | `12345678-...789abf` | JSON string | Accelerometer/gyro |
| Battery Level | `00002a19-...` | uint8_t | Battery % |

## Setup & Installation

### Prerequisites
- Flutter SDK 3.x or higher
- Dart SDK 3.x
- Android Studio / Xcode (for mobile development)
- Physical device or emulator with Bluetooth support

### Installation Steps

1. **Install Flutter**
   ```bash
   # Follow official Flutter installation guide
   # https://flutter.dev/docs/get-started/install
   ```

2. **Clone and Navigate**
   ```bash
   cd health_app
   ```

3. **Install Dependencies**
   ```bash
   flutter pub get
   ```

4. **Run the App**
   ```bash
   # For Android
   flutter run

   # For iOS
   flutter run -d ios

   # For specific device
   flutter devices  # List available devices
   flutter run -d <device-id>
   ```

## Usage

### Connecting to Device

1. **Launch App** - The app starts with device scan screen
2. **Enable Bluetooth** - Grant Bluetooth permissions if prompted
3. **Scan for Devices** - Tap "Scan" button or wait for auto-scan
4. **Select Device** - Tap "Smart Health Watch" from list
5. **Connect** - App automatically connects and navigates to dashboard

### Viewing Vitals

- **Dashboard** - Real-time vitals displayed in cards
- **Charts** - Scroll down to see trend graphs
- **Battery** - Battery level shown at bottom of dashboard

### Data Sync

- **Automatic** - Data syncs to backend every 5 minutes
- **Manual** - Pull to refresh on dashboard
- **Offline** - Data stored locally, syncs when online

## API Integration

### Backend Configuration

Edit `lib/services/api_service.dart`:
```dart
static const String baseUrl = 'http://your-backend-api.com/api/v1';
```

### Endpoints Used

- `POST /api/v1/vitals/upload` - Upload vitals data
- `GET /api/v1/vitals/latest/{user_id}` - Get latest 24h data
- `POST /api/v1/analytics/predict` - Get health predictions
- `GET /api/v1/ratings/{user_id}` - Get health ratings

## State Management

The app uses **Provider** for state management:

- **BLEService** - Manages BLE connection and data reception
- **ApiService** - Handles backend API calls
- **DatabaseService** - Singleton for local database operations

### Example Usage

```dart
// Access BLE service
final bleService = context.read<BLEService>();
final vitals = bleService.currentVitals;

// Listen to changes
Consumer<BLEService>(
  builder: (context, bleService, child) {
    return Text('HR: ${bleService.currentVitals?.heartRate}');
  },
)
```

## Local Database Schema

### vitals Table
```sql
CREATE TABLE vitals(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  heart_rate REAL,
  spo2 REAL,
  temperature REAL,
  steps INTEGER,
  battery_level REAL,
  timestamp TEXT
);
```

### imu_data Table
```sql
CREATE TABLE imu_data(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  accel_x REAL,
  accel_y REAL,
  accel_z REAL,
  gyro_x REAL,
  gyro_y REAL,
  gyro_z REAL,
  steps INTEGER,
  timestamp TEXT
);
```

## Permissions

### Android (android/app/src/main/AndroidManifest.xml)
```xml
<uses-permission android:name="android.permission.BLUETOOTH" />
<uses-permission android:name="android.permission.BLUETOOTH_ADMIN" />
<uses-permission android:name="android.permission.BLUETOOTH_SCAN" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
<uses-permission android:name="android.permission.INTERNET" />
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" />
```

### iOS (ios/Runner/Info.plist)
```xml
<key>NSBluetoothAlwaysUsageDescription</key>
<string>This app needs Bluetooth to connect to your health watch.</string>
<key>NSBluetoothPeripheralUsageDescription</key>
<string>This app needs Bluetooth to connect to your health watch.</string>
```

## Troubleshooting

### BLE Not Connecting
1. **Check Permissions** - Ensure Bluetooth permissions granted
2. **Enable Bluetooth** - Turn on device Bluetooth
3. **Restart App** - Close and reopen the app
4. **Check Device** - Ensure ESP32 is advertising BLE

### No Data Received
1. **Verify Connection** - Check if device shows as connected
2. **Check Characteristics** - Verify UUIDs match firmware
3. **Enable Notifications** - Ensure notifications enabled on characteristics
4. **Check Firmware** - Verify ESP32 is sending data

### Charts Not Displaying
1. **Check Data** - Ensure data is being received
2. **Verify Data Format** - Check if data matches expected format
3. **Update Chart Widget** - Ensure chart widget receives data

## Building for Production

### Android
```bash
flutter build apk --release
# or
flutter build appbundle --release
```

### iOS
```bash
flutter build ios --release
```

## Future Enhancements

- [ ] Sleep analysis screen
- [ ] Activity recognition display
- [ ] Health alerts and notifications
- [ ] User profile management
- [ ] Export data (CSV/PDF)
- [ ] Multi-device support
- [ ] Dark mode
- [ ] Widget support (home screen)

## License

MIT License - See main project README
