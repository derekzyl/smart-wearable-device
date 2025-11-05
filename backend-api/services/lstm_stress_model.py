"""
LSTM Model for Stress Prediction
Uses time-series data to predict stress levels
"""

import numpy as np
import tensorflow as tf
from tensorflow import keras
from tensorflow.keras.models import Sequential, load_model
from tensorflow.keras.layers import LSTM, Dense, Dropout
from typing import List, Dict, Any
import os
import joblib

class LSTMStressModel:
    """LSTM model for stress level prediction"""
    
    def __init__(self, sequence_length=60, model_path=None):
        self.sequence_length = sequence_length
        self.model = None
        self.scaler = None
        self.model_path = model_path or "models/lstm_stress_model.h5"
        self.scaler_path = "models/lstm_stress_scaler.pkl"
        
        # Create models directory if it doesn't exist
        os.makedirs("models", exist_ok=True)
        
        self._load_or_create_model()
    
    def _load_or_create_model(self):
        """Load existing model or create new one"""
        if os.path.exists(self.model_path) and os.path.exists(self.scaler_path):
            try:
                self.model = load_model(self.model_path)
                self.scaler = joblib.load(self.scaler_path)
                print("Loaded existing LSTM stress model")
            except Exception as e:
                print(f"Error loading model: {e}. Creating new model.")
                self._create_model()
        else:
            self._create_model()
    
    def _create_model(self):
        """Create new LSTM model architecture"""
        self.model = Sequential([
            LSTM(50, return_sequences=True, input_shape=(self.sequence_length, 5)),
            Dropout(0.2),
            LSTM(50, return_sequences=False),
            Dropout(0.2),
            Dense(25),
            Dense(3, activation='softmax')  # 3 classes: low, moderate, high
        ])
        
        self.model.compile(
            optimizer='adam',
            loss='categorical_crossentropy',
            metrics=['accuracy']
        )
        
        # Initialize scaler
        from sklearn.preprocessing import StandardScaler
        self.scaler = StandardScaler()
        
        print("Created new LSTM stress model")
    
    def prepare_sequences(self, data: List[Dict[str, Any]]) -> np.ndarray:
        """Prepare time-series sequences for LSTM"""
        if len(data) < self.sequence_length:
            # Pad with mean values if insufficient data
            mean_values = self._calculate_mean_values(data)
            padding = [mean_values] * (self.sequence_length - len(data))
            data = padding + data
        
        # Extract features
        features = []
        for point in data[-self.sequence_length:]:
            features.append([
                point.get('heart_rate', 70.0),
                point.get('spo2', 98.0),
                point.get('temperature', 36.5),
                point.get('accel_x', 0.0) or 0.0,
                point.get('accel_y', 0.0) or 0.0,
            ])
        
        return np.array(features).reshape(1, self.sequence_length, 5)
    
    def _calculate_mean_values(self, data: List[Dict[str, Any]]) -> Dict[str, float]:
        """Calculate mean values for padding"""
        if not data:
            return {'heart_rate': 70.0, 'spo2': 98.0, 'temperature': 36.5, 'accel_x': 0.0, 'accel_y': 0.0}
        
        return {
            'heart_rate': np.mean([d.get('heart_rate', 70.0) for d in data]),
            'spo2': np.mean([d.get('spo2', 98.0) for d in data]),
            'temperature': np.mean([d.get('temperature', 36.5) for d in data]),
            'accel_x': np.mean([d.get('accel_x', 0.0) or 0.0 for d in data]),
            'accel_y': np.mean([d.get('accel_y', 0.0) or 0.0 for d in data]),
        }
    
    def predict(self, sequences: np.ndarray) -> str:
        """Predict stress level"""
        if self.model is None:
            return "moderate"
        
        # Scale the input
        sequences_reshaped = sequences.reshape(-1, 5)
        if self.scaler is not None:
            sequences_scaled = self.scaler.fit_transform(sequences_reshaped)
        else:
            sequences_scaled = sequences_reshaped
        
        sequences_scaled = sequences_scaled.reshape(1, self.sequence_length, 5)
        
        # Predict
        prediction = self.model.predict(sequences_scaled, verbose=0)
        
        # Map to stress levels
        stress_classes = ['low', 'moderate', 'high']
        predicted_class = np.argmax(prediction[0])
        confidence = float(prediction[0][predicted_class])
        
        return stress_classes[predicted_class]
    
    def save_model(self):
        """Save model and scaler"""
        if self.model:
            self.model.save(self.model_path)
        if self.scaler:
            joblib.dump(self.scaler, self.scaler_path)

