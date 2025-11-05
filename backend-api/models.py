"""
SQLAlchemy database models
"""

from sqlalchemy import Column, Integer, Float, String, DateTime, ForeignKey, Text, JSON
from sqlalchemy.orm import relationship
from sqlalchemy.sql import func
from database import Base
import datetime

class User(Base):
    """User model"""
    __tablename__ = "users"
    
    id = Column(Integer, primary_key=True, index=True)
    username = Column(String, unique=True, index=True, nullable=False)
    email = Column(String, unique=True, index=True, nullable=False)
    hashed_password = Column(String, nullable=False)
    full_name = Column(String, nullable=True)
    age = Column(Integer, nullable=True)
    weight = Column(Float, nullable=True)  # kg
    height = Column(Float, nullable=True)  # cm
    medical_history = Column(JSON, nullable=True)  # JSON field for conditions
    created_at = Column(DateTime(timezone=True), server_default=func.now())
    updated_at = Column(DateTime(timezone=True), onupdate=func.now())
    
    vitals = relationship("VitalsData", back_populates="user")
    ratings = relationship("HealthRating", back_populates="user")

class VitalsData(Base):
    """Vitals data model (time-series)"""
    __tablename__ = "vitals_data"
    
    id = Column(Integer, primary_key=True, index=True)
    user_id = Column(Integer, ForeignKey("users.id"), nullable=False, index=True)
    timestamp = Column(DateTime(timezone=True), nullable=False, index=True, server_default=func.now())
    heart_rate = Column(Float, nullable=True)
    spo2 = Column(Float, nullable=True)
    temperature = Column(Float, nullable=True)
    steps = Column(Integer, default=0)
    accel_x = Column(Float, nullable=True)
    accel_y = Column(Float, nullable=True)
    accel_z = Column(Float, nullable=True)
    gyro_x = Column(Float, nullable=True)
    gyro_y = Column(Float, nullable=True)
    gyro_z = Column(Float, nullable=True)
    
    user = relationship("User", back_populates="vitals")

class HealthRating(Base):
    """Health ratings model"""
    __tablename__ = "health_ratings"
    
    id = Column(Integer, primary_key=True, index=True)
    user_id = Column(Integer, ForeignKey("users.id"), nullable=False, index=True)
    timestamp = Column(DateTime(timezone=True), nullable=False, server_default=func.now())
    cardiovascular_health = Column(Float, nullable=True)  # 0-100
    respiratory_efficiency = Column(Float, nullable=True)  # 0-100
    metabolic_rate = Column(String, nullable=True)  # "low"/"normal"/"high"
    fitness_age = Column(Integer, nullable=True)
    risk_score = Column(Float, nullable=True)  # 0-100
    
    user = relationship("User", back_populates="ratings")


