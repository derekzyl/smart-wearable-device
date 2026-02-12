import 'package:flutter/material.dart';

import '../services/health_calculator.dart';

/// Derived metrics card showing Health Score
class HealthScoreCard extends StatelessWidget {
  final int heartRate;
  final int spo2;
  final double temperature;
  final int hrQuality;
  final int spo2Quality;
  final int? restingHR;

  const HealthScoreCard({
    super.key,
    required this.heartRate,
    required this.spo2,
    required this.temperature,
    required this.hrQuality,
    required this.spo2Quality,
    this.restingHR,
  });

  @override
  Widget build(BuildContext context) {
    final score = HealthCalculator.calculateHealthScore(
      heartRate: heartRate,
      spo2: spo2,
      temperature: temperature,
      hrQuality: hrQuality,
      spo2Quality: spo2Quality,
      restingHR: restingHR,
    );

    final status = HealthCalculator.getHealthStatus(score);
    final emoji = HealthCalculator.getHealthStatusEmoji(score);

    return Card(
      elevation: 4,
      child: Container(
        decoration: BoxDecoration(
          gradient: LinearGradient(
            colors: [
              _getScoreColor(score),
              _getScoreColor(score).withOpacity(0.7),
            ],
            begin: Alignment.topLeft,
            end: Alignment.bottomRight,
          ),
          borderRadius: BorderRadius.circular(12),
        ),
        padding: const EdgeInsets.all(16),
        child: Column(
          children: [
            Row(
              children: [
                Text(emoji, style: const TextStyle(fontSize: 24)),
                const SizedBox(width: 8),
                const Text(
                  'Health Score',
                  style: TextStyle(
                    fontSize: 16,
                    fontWeight: FontWeight.w500,
                    color: Colors.white,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 16),
            Text(
              score.toStringAsFixed(0),
              style: const TextStyle(
                fontSize: 48,
                fontWeight: FontWeight.bold,
                color: Colors.white,
              ),
            ),
            const SizedBox(height: 4),
            Text(
              status,
              style: const TextStyle(
                fontSize: 18,
                color: Colors.white70,
                fontWeight: FontWeight.w500,
              ),
            ),
          ],
        ),
      ),
    );
  }

  Color _getScoreColor(double score) {
    if (score >= 90) return Colors.green;
    if (score >= 75) return Colors.lightGreen;
    if (score >= 60) return Colors.orange;
    return Colors.red;
  }
}

/// Compact derived metrics row
class DerivedMetricsRow extends StatelessWidget {
  final int heartRate;
  final int spo2;
  final List<int> recentHRs;

  const DerivedMetricsRow({
    super.key,
    required this.heartRate,
    required this.spo2,
    this.recentHRs = const [],
  });

  @override
  Widget build(BuildContext context) {
    final odi = HealthCalculator.calculateOxygenDeliveryIndex(
      heartRate: heartRate,
      spo2: spo2,
    );

    final hrv = HealthCalculator.calculateHRV(recentHRs);

    final cardioEff = HealthCalculator.calculateCardioEfficiency(
      heartRate: heartRate,
      spo2: spo2,
    );

    return Row(
      children: [
        Expanded(
          child: _buildMetricChip(
            label: 'ODI',
            value: odi.toStringAsFixed(1),
            icon: Icons.air,
            color: Colors.blue,
            tooltip: 'Oxygen Delivery Index',
          ),
        ),
        const SizedBox(width: 8),
        if (hrv != null)
          Expanded(
            child: _buildMetricChip(
              label: 'HRV',
              value: hrv.toStringAsFixed(0),
              icon: Icons.favorite_border,
              color: Colors.purple,
              tooltip: 'Heart Rate Variability',
            ),
          ),
        const SizedBox(width: 8),
        Expanded(
          child: _buildMetricChip(
            label: 'Cardio',
            value: cardioEff.toStringAsFixed(2),
            icon: Icons.speed,
            color: Colors.teal,
            tooltip: 'Cardiovascular Efficiency',
          ),
        ),
      ],
    );
  }

  Widget _buildMetricChip({
    required String label,
    required String value,
    required IconData icon,
    required Color color,
    required String tooltip,
  }) {
    return Tooltip(
      message: tooltip,
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
        decoration: BoxDecoration(
          color: color.withOpacity(0.1),
          borderRadius: BorderRadius.circular(8),
          border: Border.all(color: color.withOpacity(0.3)),
        ),
        child: Column(
          children: [
            Icon(icon, color: color, size: 20),
            const SizedBox(height: 4),
            Text(
              value,
              style: TextStyle(
                fontSize: 16,
                fontWeight: FontWeight.bold,
                color: color,
              ),
            ),
            Text(
              label,
              style: TextStyle(fontSize: 10, color: color.withOpacity(0.8)),
            ),
          ],
        ),
      ),
    );
  }
}
