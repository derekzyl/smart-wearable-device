/// Health Calculator Service
/// Calculates derived metrics from vital signs
class HealthCalculator {
  /// Calculate composite health score (0-100)
  /// Based on HR, SpO2, and temperature
  static double calculateHealthScore({
    required int heartRate,
    required int spo2,
    required double temperature,
    required int hrQuality,
    required int spo2Quality,
    int? restingHR,
  }) {
    double score = 100.0;

    // SpO2 Score (40 points max)
    if (spo2 >= 98) {
      score -= 0; // Perfect
    } else if (spo2 >= 95) {
      score -= (98 - spo2) * 2; // -6 at 95%
    } else if (spo2 >= 90) {
      score -= 6 + (95 - spo2) * 4; // -26 at 90%
    } else {
      score -= 40; // Critical
    }

    // Heart Rate Score (30 points max)
    if (heartRate == 0) {
      score -= 30; // No reading
    } else {
      final targetHR = restingHR ?? 70;
      final hrDiff = (heartRate - targetHR).abs();

      if (hrDiff <= 10) {
        score -= 0; // Perfect range
      } else if (hrDiff <= 20) {
        score -= (hrDiff - 10) * 1.5; // -15 at 20 diff
      } else if (hrDiff <= 40) {
        score -= 15 + (hrDiff - 20); // -35 at 40 diff
      } else {
        score -= 30; // Very abnormal
      }
    }

    // Temperature Score (20 points max)
    if (temperature >= 36.5 && temperature <= 37.5) {
      score -= 0; // Normal
    } else if (temperature >= 35.5 && temperature <= 38.0) {
      final tempDiff = (temperature - 37.0).abs();
      score -= tempDiff * 10; // -5 to -10
    } else {
      score -= 20; // Abnormal
    }

    // Quality penalty (10 points max)
    final avgQuality = (hrQuality + spo2Quality) / 2;
    if (avgQuality < 50) {
      score -= 10;
    } else if (avgQuality < 70) {
      score -= (70 - avgQuality) / 2;
    }

    return score.clamp(0, 100);
  }

  /// Calculate Heart Rate Variability (simplified RMSSD)
  /// Returns HRV in ms (higher is better)
  /// Note: Requires beat-to-beat intervals, simplified here
  static double? calculateHRV(List<int> recentHeartRates) {
    if (recentHeartRates.length < 3) return null;

    // Simplified HRV: variability in HR over recent readings
    final diffs = <double>[];
    for (int i = 1; i < recentHeartRates.length; i++) {
      diffs.add((recentHeartRates[i] - recentHeartRates[i - 1]).toDouble());
    }

    // Calculate RMSSD (root mean square of successive differences)
    final squaredDiffs = diffs.map((d) => d * d).toList();
    final meanSquared =
        squaredDiffs.reduce((a, b) => a + b) / squaredDiffs.length;
    final rmssd = meanSquared.isFinite
        ? squaredDiffs.reduce((a, b) => a + b) / squaredDiffs.length
        : 0.0;

    // Convert to pseudo-ms (scale for display)
    return rmssd * 10; // Scaled for visibility
  }

  /// Calculate Oxygen Delivery Index
  /// Formula: (HR × SpO2) / 100
  /// Higher values indicate better oxygen delivery
  static double calculateOxygenDeliveryIndex({
    required int heartRate,
    required int spo2,
  }) {
    if (heartRate == 0 || spo2 == 0) return 0.0;
    return (heartRate * spo2) / 100.0;
  }

  /// Get health status string from score
  static String getHealthStatus(double score) {
    if (score >= 90) return 'Excellent';
    if (score >= 75) return 'Good';
    if (score >= 60) return 'Fair';
    if (score >= 40) return 'Poor';
    return 'Critical';
  }

  /// Get health status color
  static String getHealthStatusEmoji(double score) {
    if (score >= 90) return '🟢';
    if (score >= 75) return '🟡';
    if (score >= 60) return '🟠';
    return '🔴';
  }

  /// Calculate stress level from HR and HRV
  static String getStressLevel({
    required int heartRate,
    double? hrv,
    int? restingHR,
  }) {
    final targetHR = restingHR ?? 70;
    final hrElevation = heartRate - targetHR;

    if (hrv != null && hrv < 20 && hrElevation > 20) {
      return 'High Stress';
    } else if (hrElevation > 15) {
      return 'Moderate Stress';
    } else if (hrElevation < -10) {
      return 'Very Relaxed';
    } else {
      return 'Relaxed';
    }
  }

  /// Calculate cardiovascular efficiency
  /// Lower is better (more oxygen per beat)
  static double calculateCardioEfficiency({
    required int heartRate,
    required int spo2,
  }) {
    if (spo2 == 0) return 0.0;
    return heartRate / spo2;
  }
}
