# Smart Health Watch - FastAPI Backend

## Overview

FastAPI backend server for the Smart Health Watch IoT wearable device. Provides RESTful API endpoints for data ingestion, real-time analytics, health predictions using machine learning, and personalized health ratings.

## Features

### Core Functionality
- **Data Ingestion** - RESTful API for receiving vitals data from devices
- **Time-series Storage** - PostgreSQL with TimescaleDB for optimized time-series queries
- **Real-time Analytics** - Statistical analysis of health metrics
- **ML-powered Predictions** - Anomaly detection, sleep classification, stress prediction
- **Health Ratings** - Cardiovascular, respiratory, and metabolic health scores
- **User Authentication** - JWT-based authentication system

### Machine Learning Models
1. **Anomaly Detection** (Isolation Forest)
   - Detects irregular heart rate patterns
   - Flags sudden SpO₂ drops
   - Identifies temperature anomalies

2. **Sleep Quality Prediction** (Heuristic-based)
   - Analyzes HR variability
   - Estimates sleep quality score (0-100)

3. **Stress Level Prediction** (Statistical Analysis)
   - Heart rate variability analysis
   - Temperature correlation
   - Classifies as low/moderate/high

4. **Activity Recognition** (Future: CNN-based)
   - Walking, running, cycling detection
   - Calorie burn estimation

## Project Structure

```
backend-api/
├── main.py                  # FastAPI application entry point
├── database.py              # Database configuration
├── models.py                # SQLAlchemy database models
├── schemas.py               # Pydantic request/response schemas
├── routes/                  # API route handlers
│   ├── vitals.py           # Vitals data endpoints
│   ├── analytics.py        # ML predictions endpoints
│   ├── ratings.py          # Health ratings endpoints
│   └── auth.py             # Authentication endpoints
├── services/                # Business logic services
│   ├── ml_service.py       # Machine learning service
│   └── rating_service.py   # Health rating calculations
├── pyproject.toml          # Python dependencies
└── README.md               # This file
```

## API Endpoints

### Authentication

#### Register User
```http
POST /api/v1/auth/register
Content-Type: application/json

{
  "username": "user123",
  "email": "user@example.com",
  "password": "secure_password",
  "full_name": "John Doe"
}
```

#### Login
```http
POST /api/v1/auth/login
Content-Type: application/x-www-form-urlencoded

username=user123&password=secure_password
```

### Vitals Data

#### Upload Vitals
```http
POST /api/v1/vitals/upload
Content-Type: application/json

{
  "user_id": "1",
  "timestamp": "2024-01-15T10:30:00Z",
  "heart_rate": 72.5,
  "spo2": 98,
  "temperature": 36.5,
  "steps": 5432,
  "accel_x": 0.1,
  "accel_y": -0.2,
  "accel_z": 9.8
}
```

#### Get Latest Vitals
```http
GET /api/v1/vitals/latest/{user_id}?hours=24
```

#### Get Vitals Statistics
```http
GET /api/v1/vitals/stats/{user_id}?days=7
```

### Analytics

#### Get Health Predictions
```http
POST /api/v1/analytics/predict
Content-Type: application/json

{
  "user_id": "1",
  "vitals_history": []  # Optional, will fetch from DB if empty
}
```

**Response:**
```json
{
  "risk_score": 35.5,
  "predictions": {
    "cardiovascular_risk": "low",
    "sleep_quality": 85,
    "stress_level": "moderate"
  },
  "recommendations": [
    "Maintain current healthy lifestyle",
    "Increase daily activity - aim for at least 10,000 steps"
  ]
}
```

### Health Ratings

#### Get Health Ratings
```http
GET /api/v1/ratings/{user_id}
```

**Response:**
```json
{
  "cardiovascular_health": 78.0,
  "respiratory_efficiency": 82.0,
  "metabolic_rate": "normal",
  "fitness_age": 32,
  "risk_score": 20.0
}
```

## Database Schema

### Users Table
```sql
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    username VARCHAR UNIQUE NOT NULL,
    email VARCHAR UNIQUE NOT NULL,
    hashed_password VARCHAR NOT NULL,
    full_name VARCHAR,
    age INTEGER,
    weight FLOAT,
    height FLOAT,
    medical_history JSONB,
    created_at TIMESTAMP DEFAULT NOW(),
    updated_at TIMESTAMP
);
```

### Vitals Data Table
```sql
CREATE TABLE vitals_data (
    id SERIAL PRIMARY KEY,
    user_id INTEGER REFERENCES users(id),
    timestamp TIMESTAMP NOT NULL,
    heart_rate FLOAT,
    spo2 FLOAT,
    temperature FLOAT,
    steps INTEGER DEFAULT 0,
    accel_x FLOAT,
    accel_y FLOAT,
    accel_z FLOAT,
    gyro_x FLOAT,
    gyro_y FLOAT,
    gyro_z FLOAT
);

-- Create index for time-series queries
CREATE INDEX idx_vitals_user_timestamp ON vitals_data(user_id, timestamp DESC);
```

### Health Ratings Table
```sql
CREATE TABLE health_ratings (
    id SERIAL PRIMARY KEY,
    user_id INTEGER REFERENCES users(id),
    timestamp TIMESTAMP DEFAULT NOW(),
    cardiovascular_health FLOAT,
    respiratory_efficiency FLOAT,
    metabolic_rate VARCHAR,
    fitness_age INTEGER,
    risk_score FLOAT
);
```

## Setup & Installation

### Prerequisites
- Python 3.11 or higher
- PostgreSQL 14+ (with TimescaleDB extension recommended)
- Redis (optional, for caching)

### Installation Steps

1. **Clone and Navigate**
   ```bash
   cd backend-api
   ```

2. **Create Virtual Environment**
   ```bash
   python -m venv venv
   source venv/bin/activate  # On Windows: venv\Scripts\activate
   ```

3. **Install Dependencies**
   ```bash
   pip install -e .
   ```

4. **Configure Environment Variables**
   Create `.env` file:
   ```env
   DATABASE_URL=postgresql+asyncpg://user:password@localhost:5432/health_watch
   SYNC_DATABASE_URL=postgresql://user:password@localhost:5432/health_watch
   SECRET_KEY=your-secret-key-change-in-production
   HOST=0.0.0.0
   PORT=8000
   ```

5. **Setup Database**
   ```bash
   # Create PostgreSQL database
   createdb health_watch
   
   # Install TimescaleDB extension (optional but recommended)
   psql -d health_watch -c "CREATE EXTENSION IF NOT EXISTS timescaledb;"
   
   # Run migrations (tables will be created automatically on first run)
   ```

6. **Run the Server**
   ```bash
   python main.py
   # or
   uvicorn main:app --reload
   ```

7. **Access API Documentation**
   - Swagger UI: http://localhost:8000/docs
   - ReDoc: http://localhost:8000/redoc

## Machine Learning Models

### Anomaly Detection

Uses **Isolation Forest** algorithm to detect unusual patterns in vitals:
- Irregular heart rate patterns (arrhythmia indicators)
- Sudden SpO₂ drops (sleep apnea screening)
- Temperature anomalies (fever detection)

### Sleep Quality Prediction

Heuristic-based analysis:
- HR variability analysis
- Activity level during sleep
- SpO₂ stability
- Returns score: 0-100

### Stress Prediction

Statistical analysis:
- Heart rate variability (HRV)
- Resting heart rate elevation
- Temperature correlation
- Classifies: "low", "moderate", "high"

### Health Ratings Calculation

- **Cardiovascular Health**: Based on HR patterns, variability
- **Respiratory Efficiency**: Based on SpO₂ levels
- **Metabolic Rate**: Based on HR, temperature, activity
- **Fitness Age**: Calculated from overall health scores

## Configuration

### Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `DATABASE_URL` | PostgreSQL async connection string | Required |
| `SYNC_DATABASE_URL` | PostgreSQL sync connection string | Required |
| `SECRET_KEY` | JWT secret key | Required |
| `HOST` | Server host | `0.0.0.0` |
| `PORT` | Server port | `8000` |
| `REDIS_URL` | Redis connection (optional) | None |

## Development

### Running Tests
```bash
pytest
```

### Code Formatting
```bash
black .
isort .
```

### Type Checking
```bash
mypy .
```

## Deployment

### Docker Deployment

Create `Dockerfile`:
```dockerfile
FROM python:3.11-slim

WORKDIR /app
COPY . .
RUN pip install -e .

CMD ["uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8000"]
```

### Production Considerations

1. **Security**
   - Change `SECRET_KEY` to strong random value
   - Enable HTTPS
   - Configure CORS properly
   - Use environment variables for secrets

2. **Database**
   - Use connection pooling
   - Enable TimescaleDB for time-series optimization
   - Set up regular backups

3. **Performance**
   - Use Redis for caching
   - Enable database connection pooling
   - Use async/await for I/O operations
   - Consider using a load balancer

4. **Monitoring**
   - Set up logging (structured logging)
   - Monitor API response times
   - Track database query performance
   - Set up health checks

## API Rate Limiting

Currently not implemented, but recommended for production:
- Use `slowapi` or `fastapi-limiter`
- Set limits per user/IP

## Troubleshooting

### Database Connection Issues
- Verify PostgreSQL is running
- Check connection string format
- Ensure database exists
- Check user permissions

### Import Errors
- Ensure virtual environment is activated
- Run `pip install -e .` again
- Check Python version (3.11+)

### ML Model Errors
- Models are initialized on first use
- Ensure scikit-learn is installed
- Check data format matches expected schema

## Implemented Features

### ✅ Machine Learning Models
- **LSTM for Stress Prediction** - Time-series LSTM model for accurate stress level classification
- **CNN for Activity Recognition** - Convolutional neural network for activity detection (walking, running, cycling, stationary)
- **Advanced Sleep Stage Classification** - Multi-stage sleep analysis (wake, light, deep, REM)

### ✅ Real-time Features
- **WebSocket Streaming** - Real-time vitals data streaming via WebSocket connections
- **Live Data Updates** - Push notifications for new vitals data

### ✅ Data Export
- **CSV Export** - Export vitals data as CSV files
- **PDF Reports** - Generate comprehensive PDF health reports with statistics

### ✅ Health Platform Integration
- **Apple Health Integration** - Import/export data in Apple Health format
- **Google Fit Integration** - Import/export data in Google Fit format

### ✅ Enhanced Authentication
- **JWT-based Authentication** - Secure token-based authentication
- **User Management** - User registration, login, and profile management

## License

MIT License - See main project README


