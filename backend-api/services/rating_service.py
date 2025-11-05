"""
Health Rating Service
Calculates health ratings based on vitals data
"""

from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy import select, func
from datetime import datetime, timedelta
from typing import Dict

from models import VitalsData, HealthRating

class RatingService:
    """Service for calculating health ratings"""
    
    async def calculate_ratings(self, user_id: int, db: AsyncSession) -> Dict:
        """Calculate health ratings for a user"""
        # Get last 7 days of data
        cutoff_time = datetime.utcnow() - timedelta(days=7)
        
        result = await db.execute(
            select(
                func.avg(VitalsData.heart_rate).label("avg_hr"),
                func.stddev(VitalsData.heart_rate).label("std_hr"),
                func.avg(VitalsData.spo2).label("avg_spo2"),
                func.avg(VitalsData.temperature).label("avg_temp"),
                func.sum(VitalsData.steps).label("total_steps"),
            )
            .where(VitalsData.user_id == user_id)
            .where(VitalsData.timestamp >= cutoff_time)
        )
        
        stats = result.first()
        
        # Calculate cardiovascular health (0-100)
        cardiovascular_health = self._calculate_cardiovascular_health(
            stats.avg_hr, stats.std_hr
        )
        
        # Calculate respiratory efficiency (0-100)
        respiratory_efficiency = self._calculate_respiratory_efficiency(
            stats.avg_spo2
        )
        
        # Determine metabolic rate
        metabolic_rate = self._determine_metabolic_rate(
            stats.avg_hr, stats.avg_temp, stats.total_steps
        )
        
        # Calculate fitness age (simplified)
        fitness_age = self._calculate_fitness_age(
            cardiovascular_health, respiratory_efficiency, stats.total_steps
        )
        
        # Overall risk score
        risk_score = 100 - ((cardiovascular_health + respiratory_efficiency) / 2)
        
        # Save to database
        rating = HealthRating(
            user_id=user_id,
            cardiovascular_health=cardiovascular_health,
            respiratory_efficiency=respiratory_efficiency,
            metabolic_rate=metabolic_rate,
            fitness_age=fitness_age,
            risk_score=risk_score,
        )
        db.add(rating)
        await db.commit()
        
        return {
            "cardiovascular_health": float(cardiovascular_health),
            "respiratory_efficiency": float(respiratory_efficiency),
            "metabolic_rate": metabolic_rate,
            "fitness_age": int(fitness_age),
            "risk_score": float(risk_score),
        }
    
    def _calculate_cardiovascular_health(self, avg_hr: float, std_hr: float) -> float:
        """Calculate cardiovascular health score"""
        if avg_hr is None:
            return 75.0
        
        # Optimal HR: 60-80 bpm
        if 60 <= avg_hr <= 80:
            base_score = 90
        elif 50 <= avg_hr < 60 or 80 < avg_hr <= 90:
            base_score = 75
        elif 40 <= avg_hr < 50 or 90 < avg_hr <= 100:
            base_score = 60
        else:
            base_score = 40
        
        # Penalize high variability
        if std_hr and std_hr > 15:
            base_score -= 10
        
        return max(0.0, min(100.0, base_score))
    
    def _calculate_respiratory_efficiency(self, avg_spo2: float) -> float:
        """Calculate respiratory efficiency score"""
        if avg_spo2 is None:
            return 80.0
        
        # Optimal SpO₂: 95-100%
        if avg_spo2 >= 98:
            return 95.0
        elif avg_spo2 >= 95:
            return 85.0
        elif avg_spo2 >= 90:
            return 70.0
        else:
            return 50.0
    
    def _determine_metabolic_rate(self, avg_hr: float, avg_temp: float, total_steps: int) -> str:
        """Determine metabolic rate category"""
        if avg_hr is None or avg_temp is None:
            return "normal"
        
        # High metabolic rate indicators
        if avg_hr > 85 and avg_temp > 37.0 and total_steps > 8000:
            return "high"
        # Low metabolic rate indicators
        elif avg_hr < 60 and avg_temp < 36.5 and total_steps < 3000:
            return "low"
        else:
            return "normal"
    
    def _calculate_fitness_age(self, cv_health: float, resp_health: float, steps: int) -> int:
        """Calculate fitness age (simplified)"""
        # Base age calculation
        base_age = 30  # Default baseline
        
        # Adjust based on health scores
        health_score = (cv_health + resp_health) / 2
        if health_score > 90:
            base_age -= 5
        elif health_score > 80:
            base_age -= 3
        elif health_score < 60:
            base_age += 5
        
        # Adjust based on activity
        if steps and steps > 10000:
            base_age -= 2
        elif steps and steps < 3000:
            base_age += 3
        
        return max(20, min(80, int(base_age)))


