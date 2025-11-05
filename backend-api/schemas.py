"""
Pydantic schemas for request/response validation
"""

from pydantic import BaseModel, EmailStr
from typing import Optional, List, Dict, Any
from datetime import datetime

# Vitals Schemas
class VitalsDataCreate(BaseModel):
    user_id: str
    timestamp: Optional[datetime] = None
    heart_rate: Optional[float] = None
    spo2: Optional[float] = None
    temperature: Optional[float] = None
    steps: Optional[int] = 0
    accel_x: Optional[float] = None
    accel_y: Optional[float] = None
    accel_z: Optional[float] = None
    gyro_x: Optional[float] = None
    gyro_y: Optional[float] = None
    gyro_z: Optional[float] = None

class VitalsDataResponse(BaseModel):
    id: int
    user_id: int
    timestamp: datetime
    heart_rate: Optional[float]
    spo2: Optional[float]
    temperature: Optional[float]
    steps: Optional[int]
    
    class Config:
        from_attributes = True

# Analytics Schemas
class HealthPredictionRequest(BaseModel):
    user_id: str
    vitals_history: Optional[List[Dict[str, Any]]] = None

class HealthPredictionResponse(BaseModel):
    risk_score: float
    predictions: Dict[str, Any]
    recommendations: List[str]

# Ratings Schemas
class HealthRatingResponse(BaseModel):
    cardiovascular_health: float
    respiratory_efficiency: float
    metabolic_rate: str
    fitness_age: int
    risk_score: Optional[float] = None

# Auth Schemas
class UserCreate(BaseModel):
    username: str
    email: EmailStr
    password: str
    full_name: Optional[str] = None

class UserLogin(BaseModel):
    username: str
    password: str

class Token(BaseModel):
    access_token: str
    token_type: str


