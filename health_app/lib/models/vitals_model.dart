/// Vital Signs Data Model
class VitalSigns {
  final int heartRate;
  final int hrQuality;
  final int spo2;
  final int spo2Quality;
  final double temperature;
  final bool tempEstimated;
  final String tempSource;
  final DateTime timestamp;
  final int batteryPercent;
  final double batteryVoltage;
  final int wifiRssi;

  VitalSigns({
    required this.heartRate,
    required this.hrQuality,
    required this.spo2,
    required this.spo2Quality,
    required this.temperature,
    required this.tempEstimated,
    required this.tempSource,
    required this.timestamp,
    required this.batteryPercent,
    required this.batteryVoltage,
    required this.wifiRssi,
  });

  factory VitalSigns.fromJson(Map<String, dynamic> json) {
    return VitalSigns(
      heartRate: json['heart_rate'] ?? 0,
      hrQuality: json['hr_quality'] ?? 0,
      spo2: json['spo2'] ?? 0,
      spo2Quality: json['spo2_quality'] ?? 0,
      temperature: (json['temperature'] ?? 0.0).toDouble(),
      tempEstimated: json['is_temp_estimated'] ?? false,
      tempSource: json['temp_source'] ?? 'UNKNOWN',
      timestamp: DateTime.parse(
        json['timestamp'] ?? DateTime.now().toIso8601String(),
      ),
      batteryPercent: json['battery_percent'] ?? 0,
      batteryVoltage: (json['battery_voltage'] ?? 0.0).toDouble(),
      wifiRssi: json['wifi_rssi'] ?? 0,
    );
  }

  bool get hasValidHR => heartRate > 0 && hrQuality > 50;
  bool get hasValidSpO2 => spo2 > 0 && spo2Quality > 50;

  // Critical alert check
  bool get hasCriticalAlert => spo2 > 0 && spo2 < 90;

  // Warning alert check
  bool get hasWarning =>
      (spo2 > 0 && spo2 < 95) ||
      heartRate > 100 ||
      (heartRate < 50 && heartRate > 0) ||
      temperature > 38.0;
}

/// Health Alert Model
class HealthAlert {
  final int id;
  final String deviceId;
  final DateTime timestamp;
  final String alertType;
  final String severity; // CRITICAL, WARNING, INFO
  final String message;
  final bool acknowledged;

  HealthAlert({
    required this.id,
    required this.deviceId,
    required this.timestamp,
    required this.alertType,
    required this.severity,
    required this.message,
    required this.acknowledged,
  });

  factory HealthAlert.fromJson(Map<String, dynamic> json) {
    return HealthAlert(
      id: json['id'],
      deviceId: json['device_id'],
      timestamp: DateTime.parse(json['timestamp']),
      alertType: json['alert_type'],
      severity: json['severity'],
      message: json['message'],
      acknowledged: json['acknowledged'] ?? false,
    );
  }

  bool get isCritical => severity == 'CRITICAL';
  bool get isWarning => severity == 'WARNING';
  bool get isInfo => severity == 'INFO';
}

/// Device Model
class HealthDevice {
  final String deviceId;
  final String? userName;
  final int? age;
  final String? gender;
  final double? restingHR;
  final DateTime? lastSeen;

  HealthDevice({
    required this.deviceId,
    this.userName,
    this.age,
    this.gender,
    this.restingHR,
    this.lastSeen,
  });

  factory HealthDevice.fromJson(Map<String, dynamic> json) {
    return HealthDevice(
      deviceId: json['device_id'],
      userName: json['user_name'],
      age: json['age'],
      gender: json['gender'],
      restingHR: json['resting_hr']?.toDouble(),
      lastSeen: json['last_seen'] != null
          ? DateTime.parse(json['last_seen'])
          : null,
    );
  }
}
