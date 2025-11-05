# Backend API - New Features Documentation

This document describes all the newly implemented features for the Smart Health Watch backend API.

## Machine Learning Enhancements

### 1. LSTM for Stress Prediction

**Location**: `services/lstm_stress_model.py`

**Description**: 
- Uses Long Short-Term Memory (LSTM) neural network for time-series stress prediction
- Analyzes patterns in heart rate, SpO₂, temperature, and movement data
- Classifies stress levels: low, moderate, high

**Usage**:
The LSTM model is automatically used in health predictions when sufficient time-series data is available (60+ data points).

**API Endpoint**: 
- `POST /api/v1/analytics/predict` - Uses LSTM internally for stress prediction

**Model Architecture**:
- Input: 60 timesteps × 5 features (HR, SpO₂, Temp, Accel X, Accel Y)
- Layers: 2 LSTM layers (50 units each) + Dropout + Dense layers
- Output: 3 classes (low/moderate/high stress)

### 2. CNN for Activity Recognition

**Location**: `services/cnn_activity_model.py`

**Description**:
- Convolutional Neural Network for activity classification
- Uses accelerometer and gyroscope data
- Recognizes: stationary, walking, running, cycling

**Usage**:
```python
POST /api/v1/analytics/activity
{
  "user_id": "1",
  "imu_data": [
    {"accel_x": 0.1, "accel_y": -0.2, "accel_z": 9.8, ...},
    ...
  ]
}
```

**Response**:
```json
{
  "activity": "walking",
  "confidence": 0.92,
  "probabilities": {
    "stationary": 0.05,
    "walking": 0.92,
    "running": 0.02,
    "cycling": 0.01
  }
}
```

**Model Architecture**:
- Input: 50 timesteps × 6 features (3-axis accel + 3-axis gyro)
- Layers: 3 Conv1D layers + MaxPooling + Dense layers
- Output: 4 activity classes

### 3. Advanced Sleep Stage Classification

**Location**: `services/ml_service.py` → `predict_sleep_stages()`

**Description**:
- Multi-stage sleep analysis using vitals and IMU data
- Classifies: wake, light sleep, deep sleep, REM sleep
- Provides probability distribution for each stage

**Usage**:
```python
POST /api/v1/analytics/sleep-stages
{
  "user_id": "1",
  "vitals_history": [...],
  "imu_data": [...]
}
```

**Response**:
```json
{
  "wake": 0.15,
  "light": 0.50,
  "deep": 0.20,
  "rem": 0.15,
  "dominant_stage": "light"
}
```

## Real-time Features

### WebSocket Streaming

**Location**: `routes/websocket.py`

**Description**:
- Real-time vitals data streaming via WebSocket connections
- Multiple clients can connect simultaneously
- Automatic connection management

**Endpoints**:

1. **General WebSocket**:
   ```
   WS /api/v1/ws/{user_id}
   ```
   - Bidirectional communication
   - Can send commands/receive updates

2. **Live Vitals Stream**:
   ```
   WS /api/v1/ws/live/{user_id}
   ```
   - Streams real-time vitals data
   - Sends initial data (last 5 minutes)
   - Keeps connection alive for updates

3. **Broadcast Endpoint**:
   ```
   POST /api/v1/ws/broadcast/{user_id}
   ```
   - Broadcasts vitals data to all connected WebSocket clients
   - Used by backend to push new data

**Example Client (JavaScript)**:
```javascript
const ws = new WebSocket('ws://localhost:8000/api/v1/ws/live/1');
ws.onmessage = (event) => {
  const data = JSON.parse(event.data);
  console.log('Heart Rate:', data.heart_rate);
  console.log('SpO₂:', data.spo2);
};
```

## Data Export

### CSV Export

**Endpoint**: `GET /api/v1/export/csv/{user_id}`

**Query Parameters**:
- `start_date` (optional): Start date for export
- `end_date` (optional): End date for export

**Response**: CSV file with vitals data

**Example**:
```
GET /api/v1/export/csv/1?start_date=2024-01-01T00:00:00&end_date=2024-01-31T23:59:59
```

**CSV Format**:
```csv
Timestamp,Heart Rate (bpm),SpO₂ (%),Temperature (°C),Steps,Accel X,Accel Y,Accel Z
2024-01-15T10:30:00,72.5,98,36.5,5432,0.1,-0.2,9.8
...
```

### PDF Reports

**Endpoint**: `GET /api/v1/export/pdf/{user_id}`

**Query Parameters**:
- `start_date` (optional): Start date for report (default: 7 days ago)
- `end_date` (optional): End date for report (default: now)

**Response**: PDF file with comprehensive health report

**Report Contents**:
- User information
- Date range
- Statistics (avg/max/min heart rate, SpO₂, temperature, total steps)
- Sample data table (first 50 records)

**Requirements**: `reportlab` package must be installed

## Health Platform Integration

### Apple Health

#### Import from Apple Health

**Endpoint**: `POST /api/v1/apple-health/import/{user_id}`

**Request Body**:
```json
[
  {
    "type": "heartRate",
    "value": 72,
    "unit": "bpm",
    "timestamp": "2024-01-15T10:30:00Z"
  },
  {
    "type": "oxygenSaturation",
    "value": 98,
    "unit": "%",
    "timestamp": "2024-01-15T10:30:00Z"
  }
]
```

**Supported Types**:
- `heartRate` → Heart rate
- `oxygenSaturation` → SpO₂
- `bodyTemperature` → Temperature
- `stepCount` → Steps

#### Export to Apple Health

**Endpoint**: `GET /api/v1/apple-health/export/{user_id}`

**Query Parameters**:
- `start_date` (optional)
- `end_date` (optional)

**Response**: JSON in Apple Health format

### Google Fit

#### Import from Google Fit

**Endpoint**: `POST /api/v1/google-fit/import/{user_id}`

**Request Body**:
```json
[
  {
    "dataTypeName": "com.google.heart_rate.bpm",
    "value": [{"fpVal": 72}],
    "startTimeNanos": "1705315800000000000",
    "endTimeNanos": "1705315800000000000"
  }
]
```

**Supported Types**:
- `com.google.heart_rate.bpm` → Heart rate
- `com.google.oxygen_saturation` → SpO₂
- `com.google.body.temperature` → Temperature
- `com.google.step_count.delta` → Steps

#### Export to Google Fit

**Endpoint**: `GET /api/v1/google-fit/export/{user_id}`

**Query Parameters**:
- `start_date` (optional)
- `end_date` (optional)

**Response**: JSON in Google Fit format

## Enhanced Authentication

### Features

- **JWT Token Authentication**: Secure token-based auth
- **User Registration**: Create new user accounts
- **User Login**: Get access tokens
- **Protected Routes**: All endpoints can require authentication

### Usage

1. **Register**:
   ```bash
   POST /api/v1/auth/register
   {
     "username": "user123",
     "email": "user@example.com",
     "password": "secure_password",
     "full_name": "John Doe"
   }
   ```

2. **Login**:
   ```bash
   POST /api/v1/auth/login
   username=user123&password=secure_password
   ```

3. **Use Token**:
   ```bash
   GET /api/v1/vitals/latest/1
   Authorization: Bearer <access_token>
   ```

## API Endpoints Summary

### Analytics
- `POST /api/v1/analytics/predict` - Health predictions (with LSTM stress)
- `POST /api/v1/analytics/activity` - Activity recognition (CNN)
- `POST /api/v1/analytics/sleep-stages` - Sleep stage classification

### WebSocket
- `WS /api/v1/ws/{user_id}` - General WebSocket
- `WS /api/v1/ws/live/{user_id}` - Live vitals stream
- `POST /api/v1/ws/broadcast/{user_id}` - Broadcast to clients

### Export
- `GET /api/v1/export/csv/{user_id}` - CSV export
- `GET /api/v1/export/pdf/{user_id}` - PDF report

### Health Platforms
- `POST /api/v1/apple-health/import/{user_id}` - Import from Apple Health
- `GET /api/v1/apple-health/export/{user_id}` - Export to Apple Health
- `POST /api/v1/google-fit/import/{user_id}` - Import from Google Fit
- `GET /api/v1/google-fit/export/{user_id}` - Export to Google Fit

## Dependencies

### New Dependencies
- `tensorflow>=2.13.0` - For LSTM and CNN models
- `reportlab>=4.0.0` - For PDF generation
- `websockets>=12.0` - For WebSocket support (FastAPI has built-in support)

### Installation
```bash
pip install -e .
# or
pip install tensorflow reportlab websockets
```

## Model Files

Models are stored in `backend-api/models/` directory:
- `lstm_stress_model.h5` - LSTM stress model
- `lstm_stress_scaler.pkl` - LSTM scaler
- `cnn_activity_model.h5` - CNN activity model
- `cnn_activity_scaler.pkl` - CNN scaler

Models are auto-created on first use if they don't exist.

## Performance Considerations

1. **LSTM Model**: Requires ~60 data points minimum for accurate predictions
2. **CNN Model**: Requires ~50 IMU data points for activity recognition
3. **WebSocket**: Supports multiple concurrent connections per user
4. **PDF Export**: Can be resource-intensive for large datasets (limited to first 50 records in sample table)

## Future Enhancements

- [ ] Model training scripts with real data
- [ ] Model versioning and A/B testing
- [ ] Real-time WebSocket event triggers
- [ ] Batch import for health platforms
- [ ] Scheduled report generation
- [ ] More health platform integrations (Samsung Health, Fitbit)

