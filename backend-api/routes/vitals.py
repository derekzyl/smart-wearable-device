"""
Vitals data routes
"""

from fastapi import APIRouter, Depends, HTTPException, status
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy import select, func
from datetime import datetime, timedelta
from typing import List

from database import get_db
from models import VitalsData
from schemas import VitalsDataCreate, VitalsDataResponse

router = APIRouter()

@router.post("/upload", response_model=dict)
async def upload_vitals(
    vitals: VitalsDataCreate,
    db: AsyncSession = Depends(get_db)
):
    """Upload vitals data from device"""
    try:
        # Get user_id from string (assuming user_id is string from device)
        # In production, map device_id to actual user_id
        user_id = int(vitals.user_id) if vitals.user_id.isdigit() else 1
        
        db_vitals = VitalsData(
            user_id=user_id,
            timestamp=vitals.timestamp or datetime.utcnow(),
            heart_rate=vitals.heart_rate,
            spo2=vitals.spo2,
            temperature=vitals.temperature,
            steps=vitals.steps,
            accel_x=vitals.accel_x,
            accel_y=vitals.accel_y,
            accel_z=vitals.accel_z,
            gyro_x=vitals.gyro_x,
            gyro_y=vitals.gyro_y,
            gyro_z=vitals.gyro_z,
        )
        
        db.add(db_vitals)
        await db.commit()
        await db.refresh(db_vitals)
        
        return {
            "status": "success",
            "data_id": db_vitals.id,
            "message": "Vitals data uploaded successfully"
        }
    except Exception as e:
        await db.rollback()
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail=f"Error uploading vitals: {str(e)}"
        )

@router.get("/latest/{user_id}", response_model=List[VitalsDataResponse])
async def get_latest_vitals(
    user_id: str,
    hours: int = 24,
    db: AsyncSession = Depends(get_db)
):
    """Get latest vitals data for a user (default: last 24 hours)"""
    try:
        user_id_int = int(user_id) if user_id.isdigit() else 1
        cutoff_time = datetime.utcnow() - timedelta(hours=hours)
        
        result = await db.execute(
            select(VitalsData)
            .where(VitalsData.user_id == user_id_int)
            .where(VitalsData.timestamp >= cutoff_time)
            .order_by(VitalsData.timestamp.desc())
            .limit(1000)
        )
        
        vitals_list = result.scalars().all()
        return vitals_list
    except Exception as e:
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail=f"Error retrieving vitals: {str(e)}"
        )

@router.get("/stats/{user_id}")
async def get_vitals_stats(
    user_id: str,
    days: int = 7,
    db: AsyncSession = Depends(get_db)
):
    """Get statistics for vitals data"""
    try:
        user_id_int = int(user_id) if user_id.isdigit() else 1
        cutoff_time = datetime.utcnow() - timedelta(days=days)
        
        result = await db.execute(
            select(
                func.avg(VitalsData.heart_rate).label("avg_hr"),
                func.max(VitalsData.heart_rate).label("max_hr"),
                func.min(VitalsData.heart_rate).label("min_hr"),
                func.avg(VitalsData.spo2).label("avg_spo2"),
                func.avg(VitalsData.temperature).label("avg_temp"),
                func.sum(VitalsData.steps).label("total_steps"),
            )
            .where(VitalsData.user_id == user_id_int)
            .where(VitalsData.timestamp >= cutoff_time)
        )
        
        stats = result.first()
        return {
            "heart_rate": {
                "average": float(stats.avg_hr) if stats.avg_hr else 0,
                "max": float(stats.max_hr) if stats.max_hr else 0,
                "min": float(stats.min_hr) if stats.min_hr else 0,
            },
            "spo2": {
                "average": float(stats.avg_spo2) if stats.avg_spo2 else 0,
            },
            "temperature": {
                "average": float(stats.avg_temp) if stats.avg_temp else 0,
            },
            "steps": {
                "total": int(stats.total_steps) if stats.total_steps else 0,
            },
        }
    except Exception as e:
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail=f"Error retrieving stats: {str(e)}"
        )


