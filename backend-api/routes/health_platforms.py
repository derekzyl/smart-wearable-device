"""
Health platform integration routes (Apple Health, Google Fit)
"""

from fastapi import APIRouter, Depends, HTTPException, Body
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy import select
from datetime import datetime, timedelta
from typing import List, Dict, Any, Optional
import json

from database import get_db
from models import VitalsData, User
from routes.auth import get_current_user
from schemas import VitalsDataResponse

router = APIRouter()

@router.post("/apple-health/import/{user_id}")
async def import_apple_health(
    user_id: int,
    health_data: List[Dict[str, Any]] = Body(...),
    db: AsyncSession = Depends(get_db),
    current_user: User = Depends(get_current_user)
):
    """Import data from Apple Health"""
    # Verify user access
    if current_user.id != user_id:
        raise HTTPException(status_code=403, detail="Access denied")
    
    imported_count = 0
    
    for data_point in health_data:
        try:
            # Parse Apple Health format
            # Expected format: {"type": "heartRate", "value": 72, "timestamp": "2024-01-15T10:30:00Z", ...}
            data_type = data_point.get("type")
            value = data_point.get("value")
            timestamp_str = data_point.get("timestamp")
            
            if not timestamp_str:
                continue
            
            timestamp = datetime.fromisoformat(timestamp_str.replace('Z', '+00:00'))
            
            # Create vitals entry
            vital = VitalsData(
                user_id=user_id,
                timestamp=timestamp,
            )
            
            # Map Apple Health types to our schema
            if data_type == "heartRate":
                vital.heart_rate = float(value)
            elif data_type == "oxygenSaturation":
                vital.spo2 = float(value)
            elif data_type == "bodyTemperature":
                vital.temperature = float(value)
            elif data_type == "stepCount":
                vital.steps = int(value)
            
            db.add(vital)
            imported_count += 1
            
        except Exception as e:
            print(f"Error importing Apple Health data: {e}")
            continue
    
    await db.commit()
    
    return {
        "status": "success",
        "imported": imported_count,
        "message": f"Imported {imported_count} data points from Apple Health"
    }

@router.get("/apple-health/export/{user_id}")
async def export_to_apple_health(
    user_id: int,
    start_date: Optional[datetime] = None,
    end_date: Optional[datetime] = None,
    db: AsyncSession = Depends(get_db),
    current_user: User = Depends(get_current_user)
):
    """Export data in Apple Health compatible format"""
    # Verify user access
    if current_user.id != user_id:
        raise HTTPException(status_code=403, detail="Access denied")
    
    # Set default date range
    if not start_date:
        start_date = datetime.utcnow() - timedelta(days=30)
    if not end_date:
        end_date = datetime.utcnow()
    
    # Query data
    result = await db.execute(
        select(VitalsData)
        .where(VitalsData.user_id == user_id)
        .where(VitalsData.timestamp >= start_date)
        .where(VitalsData.timestamp <= end_date)
        .order_by(VitalsData.timestamp)
    )
    vitals_list = result.scalars().all()
    
    # Convert to Apple Health format
    apple_health_data = []
    for vital in vitals_list:
        if vital.heart_rate:
            apple_health_data.append({
                "type": "heartRate",
                "value": vital.heart_rate,
                "unit": "bpm",
                "timestamp": vital.timestamp.isoformat(),
            })
        if vital.spo2:
            apple_health_data.append({
                "type": "oxygenSaturation",
                "value": vital.spo2,
                "unit": "%",
                "timestamp": vital.timestamp.isoformat(),
            })
        if vital.temperature:
            apple_health_data.append({
                "type": "bodyTemperature",
                "value": vital.temperature,
                "unit": "°C",
                "timestamp": vital.timestamp.isoformat(),
            })
        if vital.steps:
            apple_health_data.append({
                "type": "stepCount",
                "value": vital.steps,
                "unit": "count",
                "timestamp": vital.timestamp.isoformat(),
            })
    
    return {
        "format": "apple-health",
        "count": len(apple_health_data),
        "data": apple_health_data
    }

@router.post("/google-fit/import/{user_id}")
async def import_google_fit(
    user_id: int,
    fit_data: List[Dict[str, Any]] = Body(...),
    db: AsyncSession = Depends(get_db),
    current_user: User = Depends(get_current_user)
):
    """Import data from Google Fit"""
    # Verify user access
    if current_user.id != user_id:
        raise HTTPException(status_code=403, detail="Access denied")
    
    imported_count = 0
    
    for data_point in fit_data:
        try:
            # Parse Google Fit format
            # Expected format: {"dataTypeName": "com.google.heart_rate.bpm", "value": [{"fpVal": 72}], "startTimeNanos": "1234567890000000000", ...}
            data_type = data_point.get("dataTypeName", "")
            values = data_point.get("value", [])
            start_time_nanos = data_point.get("startTimeNanos")
            
            if not values or not start_time_nanos:
                continue
            
            # Convert nanoseconds to datetime
            timestamp = datetime.fromtimestamp(int(start_time_nanos) / 1e9)
            value = values[0].get("fpVal") or values[0].get("intVal")
            
            # Create vitals entry
            vital = VitalsData(
                user_id=user_id,
                timestamp=timestamp,
            )
            
            # Map Google Fit types to our schema
            if "heart_rate" in data_type:
                vital.heart_rate = float(value)
            elif "oxygen_saturation" in data_type:
                vital.spo2 = float(value)
            elif "body_temperature" in data_type:
                vital.temperature = float(value)
            elif "step_count" in data_type or "steps" in data_type:
                vital.steps = int(value)
            
            db.add(vital)
            imported_count += 1
            
        except Exception as e:
            print(f"Error importing Google Fit data: {e}")
            continue
    
    await db.commit()
    
    return {
        "status": "success",
        "imported": imported_count,
        "message": f"Imported {imported_count} data points from Google Fit"
    }

@router.get("/google-fit/export/{user_id}")
async def export_to_google_fit(
    user_id: int,
    start_date: Optional[datetime] = None,
    end_date: Optional[datetime] = None,
    db: AsyncSession = Depends(get_db),
    current_user: User = Depends(get_current_user)
):
    """Export data in Google Fit compatible format"""
    # Verify user access
    if current_user.id != user_id:
        raise HTTPException(status_code=403, detail="Access denied")
    
    # Set default date range
    if not start_date:
        start_date = datetime.utcnow() - timedelta(days=30)
    if not end_date:
        end_date = datetime.utcnow()
    
    # Query data
    result = await db.execute(
        select(VitalsData)
        .where(VitalsData.user_id == user_id)
        .where(VitalsData.timestamp >= start_date)
        .where(VitalsData.timestamp <= end_date)
        .order_by(VitalsData.timestamp)
    )
    vitals_list = result.scalars().all()
    
    # Convert to Google Fit format
    google_fit_data = []
    for vital in vitals_list:
        timestamp_nanos = int(vital.timestamp.timestamp() * 1e9)
        
        if vital.heart_rate:
            google_fit_data.append({
                "dataTypeName": "com.google.heart_rate.bpm",
                "value": [{"fpVal": vital.heart_rate}],
                "startTimeNanos": str(timestamp_nanos),
                "endTimeNanos": str(timestamp_nanos),
            })
        if vital.spo2:
            google_fit_data.append({
                "dataTypeName": "com.google.oxygen_saturation",
                "value": [{"fpVal": vital.spo2}],
                "startTimeNanos": str(timestamp_nanos),
                "endTimeNanos": str(timestamp_nanos),
            })
        if vital.temperature:
            google_fit_data.append({
                "dataTypeName": "com.google.body.temperature",
                "value": [{"fpVal": vital.temperature}],
                "startTimeNanos": str(timestamp_nanos),
                "endTimeNanos": str(timestamp_nanos),
            })
        if vital.steps:
            google_fit_data.append({
                "dataTypeName": "com.google.step_count.delta",
                "value": [{"intVal": vital.steps}],
                "startTimeNanos": str(timestamp_nanos),
                "endTimeNanos": str(timestamp_nanos),
            })
    
    return {
        "format": "google-fit",
        "count": len(google_fit_data),
        "data": google_fit_data
    }

