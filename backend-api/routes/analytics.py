"""
Analytics and ML prediction routes
"""

from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy import select
from datetime import datetime, timedelta

from database import get_db
from models import VitalsData
from schemas import HealthPredictionRequest, HealthPredictionResponse
from services.ml_service import MLService

router = APIRouter()
ml_service = MLService()

@router.post("/predict", response_model=HealthPredictionResponse)
async def predict_health(
    request: HealthPredictionRequest,
    db: AsyncSession = Depends(get_db)
):
    """Get health predictions based on vitals history"""
    try:
        user_id = int(request.user_id) if request.user_id.isdigit() else 1
        
        # Get recent vitals data if not provided
        if not request.vitals_history:
            cutoff_time = datetime.utcnow() - timedelta(days=7)
            result = await db.execute(
                select(VitalsData)
                .where(VitalsData.user_id == user_id)
                .where(VitalsData.timestamp >= cutoff_time)
                .order_by(VitalsData.timestamp.desc())
                .limit(1000)
            )
            vitals_list = result.scalars().all()
            vitals_history = [
                {
                    "heart_rate": v.heart_rate,
                    "spo2": v.spo2,
                    "temperature": v.temperature,
                    "timestamp": v.timestamp.isoformat(),
                }
                for v in vitals_list
            ]
        else:
            vitals_history = request.vitals_history
        
        # Get predictions from ML service
        predictions = await ml_service.predict_health(user_id, vitals_history)
        
        return HealthPredictionResponse(**predictions)
    except Exception as e:
        raise HTTPException(
            status_code=500,
            detail=f"Error generating predictions: {str(e)}"
        )


