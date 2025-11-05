"""
Health ratings routes
"""

from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy import select, desc
from datetime import datetime

from database import get_db
from models import HealthRating
from schemas import HealthRatingResponse
from services.rating_service import RatingService

router = APIRouter()
rating_service = RatingService()

@router.get("/{user_id}", response_model=HealthRatingResponse)
async def get_health_ratings(
    user_id: str,
    db: AsyncSession = Depends(get_db)
):
    """Get current health ratings for a user"""
    try:
        user_id_int = int(user_id) if user_id.isdigit() else 1
        
        # Get latest rating from database
        result = await db.execute(
            select(HealthRating)
            .where(HealthRating.user_id == user_id_int)
            .order_by(desc(HealthRating.timestamp))
            .limit(1)
        )
        
        latest_rating = result.scalar_one_or_none()
        
        if latest_rating:
            return HealthRatingResponse(
                cardiovascular_health=latest_rating.cardiovascular_health,
                respiratory_efficiency=latest_rating.respiratory_efficiency,
                metabolic_rate=latest_rating.metabolic_rate,
                fitness_age=latest_rating.fitness_age,
                risk_score=latest_rating.risk_score,
            )
        else:
            # Generate new rating if none exists
            rating = await rating_service.calculate_ratings(user_id_int, db)
            return HealthRatingResponse(**rating)
    except Exception as e:
        raise HTTPException(
            status_code=500,
            detail=f"Error retrieving ratings: {str(e)}"
        )


