"""
CNN Model for Activity Recognition
Uses accelerometer and gyroscope data to classify activities
"""

import numpy as np
import tensorflow as tf
from tensorflow import keras
from tensorflow.keras.models import Sequential, load_model
from tensorflow.keras.layers import Conv1D, MaxPooling1D, Flatten, Dense, Dropout
from typing import List, Dict, Any
import os
import joblib

class CNNActivityModel:
    """CNN model for activity recognition"""
    
    def __init__(self, window_size=50, model_path=None):
        self.window_size = window_size
        self.model = None
        self.scaler = None
        self.model_path = model_path or "models/cnn_activity_model.h5"
        self.scaler_path = "models/cnn_activity_scaler.pkl"
        
        # Activity classes
        self.activity_classes = ['stationary', 'walking', 'running', 'cycling']
        
        # Create models directory if it doesn't exist
        os.makedirs("models", exist_ok=True)
        
        self._load_or_create_model()
    
    def _load_or_create_model(self):
        """Load existing model or create new one"""
        if os.path.exists(self.model_path) and os.path.exists(self.scaler_path):
            try:
                self.model = load_model(self.model_path)
                self.scaler = joblib.load(self.scaler_path)
                print("Loaded existing CNN activity model")
            except Exception as e:
                print(f"Error loading model: {e}. Creating new model.")
                self._create_model()
        else:
            self._create_model()
    
    def _create_model(self):
        """Create new CNN model architecture"""
        self.model = Sequential([
            Conv1D(filters=64, kernel_size=3, activation='relu', input_shape=(self.window_size, 6)),
            MaxPooling1D(pool_size=2),
            Conv1D(filters=64, kernel_size=3, activation='relu'),
            MaxPooling1D(pool_size=2),
            Conv1D(filters=32, kernel_size=3, activation='relu'),
            MaxPooling1D(pool_size=2),
            Flatten(),
            Dense(100, activation='relu'),
            Dropout(0.5),
            Dense(4, activation='softmax')  # 4 activity classes
        ])
        
        self.model.compile(
            optimizer='adam',
            loss='categorical_crossentropy',
            metrics=['accuracy']
        )
        
        # Initialize scaler
        from sklearn.preprocessing import StandardScaler
        self.scaler = StandardScaler()
        
        print("Created new CNN activity model")
    
    def prepare_windows(self, imu_data: List[Dict[str, Any]]) -> np.ndarray:
        """Prepare IMU data windows for CNN"""
        if len(imu_data) < self.window_size:
            # Pad with last value if insufficient data
            last_value = imu_data[-1] if imu_data else {}
            padding = [last_value] * (self.window_size - len(imu_data))
            imu_data = padding + imu_data
        
        # Extract features (accel + gyro)
        features = []
        for point in imu_data[-self.window_size:]:
            features.append([
                point.get('accel_x', 0.0) or 0.0,
                point.get('accel_y', 0.0) or 0.0,
                point.get('accel_z', 0.0) or 0.0,
                point.get('gyro_x', 0.0) or 0.0,
                point.get('gyro_y', 0.0) or 0.0,
                point.get('gyro_z', 0.0) or 0.0,
            ])
        
        return np.array(features).reshape(1, self.window_size, 6)
    
    def predict(self, windows: np.ndarray) -> Dict[str, Any]:
        """Predict activity type"""
        if self.model is None:
            return {"activity": "stationary", "confidence": 0.5}
        
        # Scale the input
        windows_reshaped = windows.reshape(-1, 6)
        if self.scaler is not None:
            windows_scaled = self.scaler.fit_transform(windows_reshaped)
        else:
            windows_scaled = windows_reshaped
        
        windows_scaled = windows_scaled.reshape(1, self.window_size, 6)
        
        # Predict
        prediction = self.model.predict(windows_scaled, verbose=0)
        
        # Get predicted class and confidence
        predicted_class_idx = np.argmax(prediction[0])
        predicted_activity = self.activity_classes[predicted_class_idx]
        confidence = float(prediction[0][predicted_class_idx])
        
        return {
            "activity": predicted_activity,
            "confidence": confidence,
            "probabilities": {
                self.activity_classes[i]: float(prediction[0][i])
                for i in range(len(self.activity_classes))
            }
        }
    
    def save_model(self):
        """Save model and scaler"""
        if self.model:
            self.model.save(self.model_path)
        if self.scaler:
            joblib.dump(self.scaler, self.scaler_path)

