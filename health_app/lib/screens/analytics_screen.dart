import 'dart:math' as math;

import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';

import '../models/vitals_model.dart';
import '../services/health_api_service.dart';

class AnalyticsScreen extends StatefulWidget {
  final String deviceId;

  const AnalyticsScreen({super.key, required this.deviceId});

  @override
  State<AnalyticsScreen> createState() => _AnalyticsScreenState();
}

class _AnalyticsScreenState extends State<AnalyticsScreen> {
  final HealthApiService _apiService = HealthApiService();
  List<VitalSigns> _history = [];
  Map<String, dynamic>? _summaryStats;
  Map<String, dynamic>? _correlationData;
  bool _isLoading = true;
  String _selectedPeriod = '24h';

  @override
  void initState() {
    super.initState();
    _loadData();
  }

  Future<void> _loadData() async {
    setState(() => _isLoading = true);

    final endDate = DateTime.now();
    final startDate = _selectedPeriod == '24h'
        ? endDate.subtract(const Duration(hours: 24))
        : _selectedPeriod == '7d'
        ? endDate.subtract(const Duration(days: 7))
        : endDate.subtract(const Duration(days: 30));

    final history = await _apiService.getVitalsHistory(
      widget.deviceId,
      startDate: startDate,
      endDate: endDate,
      limit: _selectedPeriod == '24h' ? 288 : 1000, // 5min intervals for 24h
    );

    final stats = await _apiService.getSummaryStats(
      widget.deviceId,
      period: _selectedPeriod == '24h'
          ? 'daily'
          : _selectedPeriod == '7d'
          ? 'weekly'
          : 'monthly',
    );

    final correlation = await _apiService.getCorrelationAnalysis(
      widget.deviceId,
      hours: _selectedPeriod == '24h'
          ? 24
          : (_selectedPeriod == '7d' ? 168 : 720),
    );

    if (mounted) {
      setState(() {
        _history = history;
        _summaryStats = stats;
        _correlationData = correlation;
        _isLoading = false;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Analytics'),
        actions: [
          PopupMenuButton<String>(
            initialValue: _selectedPeriod,
            onSelected: (value) {
              setState(() => _selectedPeriod = value);
              _loadData();
            },
            itemBuilder: (context) => [
              const PopupMenuItem(value: '24h', child: Text('Last 24 Hours')),
              const PopupMenuItem(value: '7d', child: Text('Last 7 Days')),
              const PopupMenuItem(value: '30d', child: Text('Last 30 Days')),
            ],
          ),
        ],
      ),
      body: _isLoading
          ? const Center(child: CircularProgressIndicator())
          : RefreshIndicator(
              onRefresh: _loadData,
              child: ListView(
                padding: const EdgeInsets.all(16),
                children: [
                  // Summary Stats Cards
                  if (_summaryStats != null) ...[
                    _buildSummarySection(),
                    const SizedBox(height: 24),
                  ],

                  // Heart Rate Chart
                  _buildChartCard(
                    title: 'Heart Rate Trend',
                    unit: 'BPM',
                    color: Colors.red,
                    data: _history
                        .map(
                          (v) => FlSpot(
                            v.timestamp.millisecondsSinceEpoch.toDouble(),
                            v.heartRate.toDouble(),
                          ),
                        )
                        .toList(),
                  ),
                  const SizedBox(height: 16),

                  // SpO2 Chart
                  _buildChartCard(
                    title: 'Blood Oxygen (SpO2)',
                    unit: '%',
                    color: Colors.blue,
                    data: _history
                        .where((v) => v.spo2 > 0)
                        .map(
                          (v) => FlSpot(
                            v.timestamp.millisecondsSinceEpoch.toDouble(),
                            v.spo2.toDouble(),
                          ),
                        )
                        .toList(),
                    minY: 85,
                    maxY: 100,
                  ),
                  const SizedBox(height: 16),

                  // Temperature Chart
                  _buildChartCard(
                    title: 'Body Temperature',
                    unit: '°C',
                    color: Colors.orange,
                    data: _history
                        .map(
                          (v) => FlSpot(
                            v.timestamp.millisecondsSinceEpoch.toDouble(),
                            v.temperature,
                          ),
                        )
                        .toList(),
                    minY: 35,
                    maxY: 40,
                  ),
                  const SizedBox(height: 24),

                  // Health Patterns
                  if (_correlationData != null &&
                      _correlationData!['patterns'] != null) ...[
                    _buildPatternsSection(),
                  ],
                ],
              ),
            ),
    );
  }

  Widget _buildSummarySection() {
    final hrStats = _summaryStats!['heart_rate'] ?? {};
    final spo2Stats = _summaryStats!['spo2'] ?? {};
    final tempStats = _summaryStats!['temperature'] ?? {};

    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'Summary Statistics',
              style: Theme.of(
                context,
              ).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 16),
            Row(
              children: [
                Expanded(
                  child: _buildStatBox(
                    'Heart Rate',
                    '${hrStats['avg'] ?? 0}',
                    'avg BPM',
                    Colors.red,
                    min: hrStats['min'],
                    max: hrStats['max'],
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: _buildStatBox(
                    'SpO2',
                    '${spo2Stats['avg'] ?? 0}',
                    'avg %',
                    Colors.blue,
                    min: spo2Stats['min'],
                  ),
                ),
              ],
            ),
            const SizedBox(height: 12),
            Row(
              children: [
                Expanded(
                  child: _buildStatBox(
                    'Temperature',
                    '${tempStats['avg'] ?? 0}',
                    'avg °C',
                    Colors.orange,
                    min: tempStats['min'],
                    max: tempStats['max'],
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: _buildStatBox(
                    'Readings',
                    '${_summaryStats!['total_readings'] ?? 0}',
                    'total',
                    Colors.green,
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildStatBox(
    String title,
    String value,
    String subtitle,
    Color color, {
    dynamic min,
    dynamic max,
  }) {
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: color.withOpacity(0.1),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: color.withOpacity(0.3)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            title,
            style: TextStyle(fontSize: 12, color: Colors.grey.shade700),
          ),
          const SizedBox(height: 4),
          Text(
            value,
            style: TextStyle(
              fontSize: 24,
              fontWeight: FontWeight.bold,
              color: color,
            ),
          ),
          Text(subtitle, style: const TextStyle(fontSize: 10)),
          if (min != null || max != null) ...[
            const SizedBox(height: 4),
            Text(
              'Range: ${min ?? '-'} - ${max ?? '-'}',
              style: TextStyle(fontSize: 10, color: Colors.grey.shade600),
            ),
          ],
        ],
      ),
    );
  }

  Widget _buildChartCard({
    required String title,
    required String unit,
    required Color color,
    required List<FlSpot> data,
    double? minY,
    double? maxY,
  }) {
    if (data.isEmpty) {
      return Card(
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            children: [
              Text(title, style: const TextStyle(fontWeight: FontWeight.bold)),
              const SizedBox(height: 16),
              const Text('No data available'),
            ],
          ),
        ),
      );
    }

    final minYValue = minY ?? data.map((s) => s.y).reduce(math.min) - 5;
    final maxYValue = maxY ?? data.map((s) => s.y).reduce(math.max) + 5;

    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              title,
              style: const TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 16),
            SizedBox(
              height: 200,
              child: LineChart(
                LineChartData(
                  gridData: FlGridData(
                    show: true,
                    drawVerticalLine: false,
                    horizontalInterval: (maxYValue - minYValue) / 4,
                  ),
                  titlesData: FlTitlesData(
                    leftTitles: AxisTitles(
                      sideTitles: SideTitles(
                        showTitles: true,
                        reservedSize: 40,
                        getTitlesWidget: (value, meta) => Text(
                          value.toInt().toString(),
                          style: const TextStyle(fontSize: 10),
                        ),
                      ),
                    ),
                    bottomTitles: AxisTitles(
                      sideTitles: SideTitles(
                        showTitles: true,
                        reservedSize: 30,
                        getTitlesWidget: (value, meta) {
                          final time = DateTime.fromMillisecondsSinceEpoch(
                            value.toInt(),
                          );
                          return Text(
                            '${time.hour}:${time.minute.toString().padLeft(2, '0')}',
                            style: const TextStyle(fontSize: 9),
                          );
                        },
                      ),
                    ),
                    rightTitles: const AxisTitles(
                      sideTitles: SideTitles(showTitles: false),
                    ),
                    topTitles: const AxisTitles(
                      sideTitles: SideTitles(showTitles: false),
                    ),
                  ),
                  borderData: FlBorderData(show: false),
                  minY: minYValue,
                  maxY: maxYValue,
                  lineBarsData: [
                    LineChartBarData(
                      spots: data,
                      isCurved: true,
                      color: color,
                      barWidth: 3,
                      dotData: const FlDotData(show: false),
                      belowBarData: BarAreaData(
                        show: true,
                        color: color.withOpacity(0.2),
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildPatternsSection() {
    final patterns = _correlationData!['patterns'] as List;

    if (patterns.isEmpty) return const SizedBox.shrink();

    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'Health Patterns Detected',
              style: Theme.of(
                context,
              ).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 12),
            ...patterns.map((pattern) {
              final severity = pattern['severity'] as String;
              final color = severity == 'CRITICAL'
                  ? Colors.red
                  : severity == 'WARNING'
                  ? Colors.orange
                  : Colors.blue;

              return Padding(
                padding: const EdgeInsets.only(bottom: 8),
                child: Row(
                  children: [
                    Icon(
                      severity == 'CRITICAL'
                          ? Icons.error
                          : severity == 'WARNING'
                          ? Icons.warning
                          : Icons.info,
                      color: color,
                      size: 20,
                    ),
                    const SizedBox(width: 8),
                    Expanded(
                      child: Text(
                        pattern['message'] as String,
                        style: TextStyle(color: color),
                      ),
                    ),
                  ],
                ),
              );
            }),
          ],
        ),
      ),
    );
  }
}
