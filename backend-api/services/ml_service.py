"""
Machine Learning Service
Implements anomaly detection, sleep classification, stress prediction, and activity recognition
"""

import numpy as np
import pandas as pd
from typing import List, Dict, Any
from sklearn.ensemble import IsolationForest, RandomForestClassifier
from sklearn.preprocessing import StandardScaler
import joblib
import os
from .lstm_stress_model import LSTMStressModel
from .cnn_activity_model import CNNActivityModel

class MLService:
    """ML service for health predictions"""
    
    def __init__(self):
        self.anomaly_model = None
        self.sleep_model = None
        self.scaler = StandardScaler()
        self.lstm_stress_model = LSTMStressModel()
        self.cnn_activity_model = CNNActivityModel()
        self._load_models()
    
    def _load_models(self):
        """Load trained models (or initialize if not exists)"""
        # In production, load from saved model files
        # For now, initialize new models
        self.anomaly_model = IsolationForest(contamination=0.1, random_state=42)
        self.sleep_model = RandomForestClassifier(n_estimators=100, random_state=42)
    
    async def predict_health(self, user_id: int, vitals_history: List[Dict[str, Any]]) -> Dict[str, Any]:
        """Generate health predictions"""
        if not vitals_history:
            return self._default_predictions()
        
        df = pd.DataFrame(vitals_history)
        
        # Anomaly detection
        anomalies = self._detect_anomalies(df)
        
        # Risk score calculation
        risk_score = self._calculate_risk_score(df, anomalies)
        
        # Sleep quality prediction
        sleep_quality = self._predict_sleep_quality(df)
        
        # Stress level prediction (using LSTM)
        stress_level = self._predict_stress_level_lstm(vitals_history)
        
        # Cardiovascular risk
        cardiovascular_risk = self._predict_cardiovascular_risk(df)
        
        # Generate recommendations
        recommendations = self._generate_recommendations(
            risk_score, sleep_quality, stress_level, df
        )
        
        return {
            "risk_score": float(risk_score),
            "predictions": {
                "cardiovascular_risk": cardiovascular_risk,
                "sleep_quality": sleep_quality,
                "stress_level": stress_level,
            },
            "recommendations": recommendations,
        }
    
    def _detect_anomalies(self, df: pd.DataFrame) -> List[bool]:
        """Detect anomalies in vitals data"""
        if df.empty or 'heart_rate' not in df.columns:
            return []
        
        # Prepare features
        features = ['heart_rate', 'spo2', 'temperature']
        available_features = [f for f in features if f in df.columns]
        
        if not available_features:
            return []
        
        X = df[available_features].fillna(df[available_features].mean())
        
        # Fit and predict
        if self.anomaly_model is None:
            self.anomaly_model = IsolationForest(contamination=0.1, random_state=42)
        
        predictions = self.anomaly_model.fit_predict(X)
        return predictions == -1  # -1 indicates anomaly
    
    def _calculate_risk_score(self, df: pd.DataFrame, anomalies: List[bool]) -> float:
        """Calculate overall health risk score (0-100)"""
        if df.empty:
            return 50.0
        
        risk = 50.0  # Base risk
        
        # Heart rate anomalies
        if 'heart_rate' in df.columns:
            hr_mean = df['heart_rate'].mean()
            if hr_mean > 100 or hr_mean < 60:
                risk += 10
            if any(anomalies):
                risk += 15
        
        # SpO₂ anomalies
        if 'spo2' in df.columns:
            spo2_mean = df['spo2'].mean()
            if spo2_mean < 95:
                risk += 20
        
        # Temperature anomalies
        if 'temperature' in df.columns:
            temp_mean = df['temperature'].mean()
            if temp_mean > 37.5 or temp_mean < 36.0:
                risk += 10
        
        return min(100.0, max(0.0, risk))
    
    def _predict_sleep_quality(self, df: pd.DataFrame) -> int:
        """Predict sleep quality (0-100)"""
        if df.empty:
            return 75
        
        # Simple heuristic based on HR variability and activity
        if 'heart_rate' in df.columns:
            hr_std = df['heart_rate'].std()
            if hr_std < 5:
                return 90  # Stable HR indicates good sleep
            elif hr_std < 10:
                return 75
            else:
                return 60
        
        return 75
    
    def predict_sleep_stages(self, vitals_history: List[Dict[str, Any]], imu_data: List[Dict[str, Any]]) -> Dict[str, Any]:
        """Advanced sleep stage classification"""
        if not vitals_history or not imu_data:
            return {
                "wake": 0.2,
                "light": 0.3,
                "deep": 0.3,
                "rem": 0.2,
                "dominant_stage": "light"
            }
        
        df_vitals = pd.DataFrame(vitals_history)
        df_imu = pd.DataFrame(imu_data)
        
        # Features for sleep stage classification
        features = []
        
        # Heart rate features
        if 'heart_rate' in df_vitals.columns and not df_vitals['heart_rate'].empty:
            hr_mean = df_vitals['heart_rate'].mean()
            hr_std = df_vitals['heart_rate'].std()
            features.extend([hr_mean, hr_std])
        else:
            features.extend([70.0, 5.0])
        
        # Movement features from IMU
        if 'accel_x' in df_imu.columns and not df_imu.empty:
            movement = np.sqrt(
                df_imu['accel_x']**2 + 
                df_imu['accel_y']**2 + 
                df_imu['accel_z']**2
            ).mean()
            features.append(movement)
        else:
            features.append(0.1)
        
        # SpO₂ features
        if 'spo2' in df_vitals.columns and not df_vitals['spo2'].empty:
            spo2_mean = df_vitals['spo2'].mean()
            spo2_std = df_vitals['spo2'].std()
            features.extend([spo2_mean, spo2_std])
        else:
            features.extend([98.0, 1.0])
        
        # Use Random Forest for classification
        if self.sleep_model is None:
            self.sleep_model = RandomForestClassifier(n_estimators=100, random_state=42)
        
        # Prepare features for prediction
        X = np.array(features).reshape(1, -1)
        
        # Since we don't have trained data, use heuristic-based classification
        # In production, this would use a trained model
        hr_mean = features[0]
        hr_std = features[1]
        movement = features[2]
        
        # Heuristic classification
        if movement > 0.5:
            # High movement = wake
            probabilities = [0.7, 0.2, 0.05, 0.05]
            dominant = "wake"
        elif hr_std < 3 and hr_mean < 65:
            # Very stable, low HR = deep sleep
            probabilities = [0.05, 0.1, 0.7, 0.15]
            dominant = "deep"
        elif hr_std < 5 and 60 < hr_mean < 75:
            # Moderate variability = REM
            probabilities = [0.1, 0.2, 0.2, 0.5]
            dominant = "rem"
        else:
            # Light sleep
            probabilities = [0.15, 0.5, 0.2, 0.15]
            dominant = "light"
        
        return {
            "wake": probabilities[0],
            "light": probabilities[1],
            "deep": probabilities[2],
            "rem": probabilities[3],
            "dominant_stage": dominant
        }
    
    def _predict_stress_level(self, df: pd.DataFrame) -> str:
        """Predict stress level (fallback method)"""
        if df.empty or 'heart_rate' not in df.columns:
            return "moderate"
        
        hr_mean = df['heart_rate'].mean()
        hr_std = df['heart_rate'].std()
        
        if hr_mean > 90 or hr_std > 15:
            return "high"
        elif hr_mean > 75 or hr_std > 10:
            return "moderate"
        else:
            return "low"
    
    def _predict_stress_level_lstm(self, vitals_history: List[Dict[str, Any]]) -> str:
        """Predict stress level using LSTM model"""
        if not vitals_history or len(vitals_history) < 10:
            # Fallback to simple method if insufficient data
            if vitals_history:
                df = pd.DataFrame(vitals_history)
                return self._predict_stress_level(df)
            return "moderate"
        
        try:
            sequences = self.lstm_stress_model.prepare_sequences(vitals_history)
            stress_level = self.lstm_stress_model.predict(sequences)
            return stress_level
        except Exception as e:
            print(f"LSTM prediction error: {e}. Using fallback method.")
            if vitals_history:
                df = pd.DataFrame(vitals_history)
                return self._predict_stress_level(df)
            return "moderate"
    
    def predict_activity(self, imu_data: List[Dict[str, Any]]) -> Dict[str, Any]:
        """Predict activity using CNN model"""
        if not imu_data or len(imu_data) < 10:
            return {"activity": "stationary", "confidence": 0.5}
        
        try:
            windows = self.cnn_activity_model.prepare_windows(imu_data)
            activity_prediction = self.cnn_activity_model.predict(windows)
            return activity_prediction
        except Exception as e:
            print(f"CNN prediction error: {e}")
            return {"activity": "stationary", "confidence": 0.5}
    
    def _predict_cardiovascular_risk(self, df: pd.DataFrame) -> str:
        """Predict cardiovascular risk"""
        if df.empty or 'heart_rate' not in df.columns:
            return "low"
        
        hr_mean = df['heart_rate'].mean()
        
        if hr_mean > 100:
            return "high"
        elif hr_mean > 85:
            return "moderate"
        else:
            return "low"
    
    def _generate_recommendations(
        self,
        risk_score: float,
        sleep_quality: int,
        stress_level: str,
        df: pd.DataFrame
    ) -> List[str]:
        """Generate personalized health recommendations"""
        recommendations = []
        
        if risk_score > 70:
            recommendations.append("Consider consulting a healthcare professional")
        
        if sleep_quality < 70:
            recommendations.append("Improve sleep hygiene - aim for 7-9 hours of sleep")
        
        if stress_level == "high":
            recommendations.append("Practice stress-reduction techniques (meditation, deep breathing)")
        
        if 'steps' in df.columns and df['steps'].sum() < 5000:
            recommendations.append("Increase daily activity - aim for at least 10,000 steps")
        
        if 'spo2' in df.columns and df['spo2'].mean() < 95:
            recommendations.append("Ensure adequate oxygen intake - consider breathing exercises")
        
        if 'temperature' in df.columns:
            temp_mean = df['temperature'].mean()
            if temp_mean > 37.5:
                recommendations.append("Monitor temperature - consider rest and hydration")
        
        if not recommendations:
            recommendations.append("Maintain current healthy lifestyle")
        
        return recommendations
    
    def _default_predictions(self) -> Dict[str, Any]:
        """Return default predictions when no data available"""
        return {
            "risk_score": 50.0,
            "predictions": {
                "cardiovascular_risk": "low",
                "sleep_quality": 75,
                "stress_level": "moderate",
            },
            "recommendations": ["Collect more data for accurate predictions"],
        }


