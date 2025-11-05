class VitalsData {
  final double heartRate;
  final double spo2;
  final double temperature;
  final int steps;
  final double batteryLevel;
  final DateTime timestamp;

  VitalsData({
    required this.heartRate,
    required this.spo2,
    required this.temperature,
    required this.steps,
    required this.batteryLevel,
    required this.timestamp,
  });

  Map<String, dynamic> toJson() {
    return {
      'heart_rate': heartRate,
      'spo2': spo2,
      'temperature': temperature,
      'steps': steps,
      'battery_level': batteryLevel,
      'timestamp': timestamp.toIso8601String(),
    };
  }

  factory VitalsData.fromJson(Map<String, dynamic> json) {
    return VitalsData(
      heartRate: (json['heart_rate'] ?? 0.0).toDouble(),
      spo2: (json['spo2'] ?? 0.0).toDouble(),
      temperature: (json['temperature'] ?? 0.0).toDouble(),
      steps: json['steps'] ?? 0,
      batteryLevel: (json['battery_level'] ?? 0.0).toDouble(),
      timestamp: DateTime.parse(json['timestamp']),
    );
  }
}

class IMUData {
  final double accelX;
  final double accelY;
  final double accelZ;
  final double gyroX;
  final double gyroY;
  final double gyroZ;
  final int steps;
  final DateTime timestamp;

  IMUData({
    required this.accelX,
    required this.accelY,
    required this.accelZ,
    required this.gyroX,
    required this.gyroY,
    required this.gyroZ,
    required this.steps,
    required this.timestamp,
  });

  Map<String, dynamic> toJson() {
    return {
      'accel_x': accelX,
      'accel_y': accelY,
      'accel_z': accelZ,
      'gyro_x': gyroX,
      'gyro_y': gyroY,
      'gyro_z': gyroZ,
      'steps': steps,
      'timestamp': timestamp.toIso8601String(),
    };
  }

  factory IMUData.fromJson(Map<String, dynamic> json) {
    return IMUData(
      accelX: (json['accel_x'] ?? 0.0).toDouble(),
      accelY: (json['accel_y'] ?? 0.0).toDouble(),
      accelZ: (json['accel_z'] ?? 0.0).toDouble(),
      gyroX: (json['gyro_x'] ?? 0.0).toDouble(),
      gyroY: (json['gyro_y'] ?? 0.0).toDouble(),
      gyroZ: (json['gyro_z'] ?? 0.0).toDouble(),
      steps: json['steps'] ?? 0,
      timestamp: DateTime.parse(json['timestamp']),
    );
  }
}

