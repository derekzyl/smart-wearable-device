# Health Monitoring Flutter App

## Configuration

### Backend API
The app is configured to use the Koyeb backend:
- **Base URL**: `https://xenophobic-netta-cybergenii-1584fde7.koyeb.app`
- **Endpoints**: All `/health/*` endpoints

### Features Implemented

#### 1. Device Setup Screen
- Enter ESP32 Device ID
- Quick start instructions
- Navigate to dashboard

#### 2. Dashboard Screen
- **Real-time Vital Cards**:
  - Heart Rate (BPM) with signal quality
  - SpO2 (%) with signal quality
  - Temperature (°C) with source indicator
  - Battery (%) with icon

- **Critical Alerts Banner**:
  - Displays prominently for SpO2 < 90%
  - Shows all critical alerts from backend

- **Auto-refresh**: Every 5 seconds
- **Pull-to-refresh**: Manual refresh gesture
- **Color-coded vitals**:
  - Red: Critical
  - Orange: Warning
  - Green: Normal
  - Grey: No data

## Running the App

### Prerequisites
```bash
flutter pub get
```

### Android
```bash
flutter run -d android
```

### iOS
```bash
flutter run -d ios
```

### Web
```bash
flutter run -d chrome
```

## Project Structure

```
lib/
├── config/
│   └── api_config.dart          # API endpoints and configuration
├── models/
│   └── vitals_model.dart        # Data models (VitalSigns, HealthAlert, HealthDevice)
├── services/
│   └── health_api_service.dart  # API service layer
├── screens/
│   └── dashboard_screen.dart    # Main dashboard UI
└── main.dart                     # App entry point & device setup
```

## Usage Flow

1. **Launch App**: Shows device setup screen
2. **Enter Device ID**: Type ESP32 device ID (from Serial Monitor)
3. **View Dashboard**: Real-time vitals display
4. **Critical Alerts**: Red banner shows SpO2 < 90% alerts
5. **Auto-refresh**: Vitals update every 5 seconds

## API Integration

### Endpoints Used
- `GET /health/vitals/{device_id}/latest` - Latest vitals (every 5s)
- `GET /health/alerts/{device_id}/critical` - Critical alerts

### Future Enhancements
- `GET /health/vitals/{device_id}/history` - For trend charts
- `POST /health/devices/{device_id}/calibrate` - Set resting HR
- `GET /health/analytics/{device_id}/summary` - Statistics

## Notes

- **Lint Errors**: Will resolve after `flutter pub get`
- **Backend**: Ensure Koyeb backend is running
- **Device ID Format**: ESP32_XXXXXX (from firmware Serial output)
- **Network**: App requires internet connection

## Testing

1. Power on ESP32 with firmware
2. Note Device ID from Serial Monitor
3. Launch Flutter app
4. Enter Device ID
5. Verify vitals display on dashboard
6. Test critical alerts (SpO2 < 90% simulation)

## Troubleshooting

### "No data available yet"
- Check ESP32 is powered on and connected to WiFi
- Verify device ID is correct
- Check backend is reachable

### Lint errors in IDE
- Run `flutter pub get`
- Restart IDE/LSP server

### Connection errors
- Verify Koyeb backend URL in `api_config.dart`
- Check internet connection
- Ensure backend `/health/vitals` endpoint is accessible
