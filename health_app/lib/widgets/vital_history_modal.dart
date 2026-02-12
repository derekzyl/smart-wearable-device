import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';
import 'package:intl/intl.dart';

import '../models/vitals_model.dart';
import '../services/health_api_service.dart';

/// Detailed history modal for a specific vital sign
class VitalHistoryModal extends StatefulWidget {
  final String deviceId;
  final String vitalType; // 'hr', 'spo2', 'temp'
  final String title;
  final Color color;

  const VitalHistoryModal({
    super.key,
    required this.deviceId,
    required this.vitalType,
    required this.title,
    required this.color,
  });

  @override
  State<VitalHistoryModal> createState() => _VitalHistoryModalState();
}

class _VitalHistoryModalState extends State<VitalHistoryModal> {
  final HealthApiService _apiService = HealthApiService();
  List<VitalSigns> _history = [];
  bool _isLoading = true;
  String _timeRange = '1h'; // 1h, 6h, 24h

  @override
  void initState() {
    super.initState();
    _loadHistory();
  }

  Future<void> _loadHistory() async {
    setState(() => _isLoading = true);

    final hours = _timeRange == '1h'
        ? 1
        : _timeRange == '6h'
        ? 6
        : 24;
    final startDate = DateTime.now().subtract(Duration(hours: hours));

    final history = await _apiService.getVitalsHistory(
      widget.deviceId,
      limit: 100,
      startDate: startDate,
    );

    if (mounted) {
      setState(() {
        _history = history;
        _isLoading = false;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      height: MediaQuery.of(context).size.height * 0.8,
      decoration: const BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.vertical(top: Radius.circular(20)),
      ),
      child: Column(
        children: [
          // Header
          Container(
            padding: const EdgeInsets.all(16),
            decoration: BoxDecoration(
              color: widget.color.withOpacity(0.1),
              borderRadius: const BorderRadius.vertical(
                top: Radius.circular(20),
              ),
            ),
            child: Row(
              children: [
                Icon(Icons.show_chart, color: widget.color),
                const SizedBox(width: 12),
                Expanded(
                  child: Text(
                    widget.title,
                    style: TextStyle(
                      fontSize: 20,
                      fontWeight: FontWeight.bold,
                      color: widget.color,
                    ),
                  ),
                ),
                IconButton(
                  icon: const Icon(Icons.close),
                  onPressed: () => Navigator.pop(context),
                ),
              ],
            ),
          ),

          // Time range selector
          Padding(
            padding: const EdgeInsets.all(8),
            child: SegmentedButton<String>(
              segments: const [
                ButtonSegment(value: '1h', label: Text('1H')),
                ButtonSegment(value: '6h', label: Text('6H')),
                ButtonSegment(value: '24h', label: Text('24H')),
              ],
              selected: {_timeRange},
              onSelectionChanged: (Set<String> selection) {
                setState(() => _timeRange = selection.first);
                _loadHistory();
              },
            ),
          ),

          // Chart
          Expanded(
            child: _isLoading
                ? const Center(child: CircularProgressIndicator())
                : _history.isEmpty
                ? const Center(child: Text('No data available'))
                : Padding(
                    padding: const EdgeInsets.all(16),
                    child: LineChart(
                      LineChartData(
                        gridData: FlGridData(
                          show: true,
                          drawVerticalLine: false,
                          horizontalInterval: _getInterval(),
                        ),
                        titlesData: FlTitlesData(
                          leftTitles: AxisTitles(
                            sideTitles: SideTitles(
                              showTitles: true,
                              reservedSize: 40,
                              interval: _getInterval(),
                            ),
                          ),
                          bottomTitles: AxisTitles(
                            sideTitles: SideTitles(
                              showTitles: true,
                              reservedSize: 30,
                              getTitlesWidget: (value, meta) {
                                final index = value.toInt();
                                if (index < 0 || index >= _history.length) {
                                  return const Text('');
                                }
                                final time = _history[index].timestamp;
                                return Text(
                                  DateFormat('HH:mm').format(time),
                                  style: const TextStyle(fontSize: 10),
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
                        borderData: FlBorderData(
                          show: true,
                          border: Border.all(color: Colors.grey.shade300),
                        ),
                        lineBarsData: [
                          LineChartBarData(
                            spots: _getSpots(),
                            isCurved: true,
                            color: widget.color,
                            barWidth: 3,
                            dotData: const FlDotData(show: true),
                            belowBarData: BarAreaData(
                              show: true,
                              color: widget.color.withOpacity(0.1),
                            ),
                          ),
                        ],
                      ),
                    ),
                  ),
          ),

          // Statistics
          Container(
            padding: const EdgeInsets.all(16),
            decoration: BoxDecoration(
              color: Colors.grey.shade100,
              borderRadius: const BorderRadius.vertical(
                bottom: Radius.circular(20),
              ),
            ),
            child: _buildStatistics(),
          ),
        ],
      ),
    );
  }

  List<FlSpot> _getSpots() {
    return _history.asMap().entries.map((entry) {
      final index = entry.key;
      final vital = entry.value;
      double value;

      switch (widget.vitalType) {
        case 'hr':
          value = vital.heartRate.toDouble();
          break;
        case 'spo2':
          value = vital.spo2.toDouble();
          break;
        case 'temp':
          value = vital.temperature;
          break;
        default:
          value = 0;
      }

      return FlSpot(index.toDouble(), value);
    }).toList();
  }

  double _getInterval() {
    switch (widget.vitalType) {
      case 'hr':
        return 20;
      case 'spo2':
        return 2;
      case 'temp':
        return 0.5;
      default:
        return 10;
    }
  }

  Widget _buildStatistics() {
    if (_history.isEmpty) return const SizedBox.shrink();

    final values = _history.map((v) {
      switch (widget.vitalType) {
        case 'hr':
          return v.heartRate.toDouble();
        case 'spo2':
          return v.spo2.toDouble();
        case 'temp':
          return v.temperature;
        default:
          return 0.0;
      }
    }).toList();

    final avg = values.reduce((a, b) => a + b) / values.length;
    final max = values.reduce((a, b) => a > b ? a : b);
    final min = values.reduce((a, b) => a < b ? a : b);

    return Row(
      mainAxisAlignment: MainAxisAlignment.spaceAround,
      children: [
        _buildStatItem('Average', avg.toStringAsFixed(1), Icons.analytics),
        _buildStatItem('Max', max.toStringAsFixed(1), Icons.arrow_upward),
        _buildStatItem('Min', min.toStringAsFixed(1), Icons.arrow_downward),
        _buildStatItem('Readings', values.length.toString(), Icons.data_usage),
      ],
    );
  }

  Widget _buildStatItem(String label, String value, IconData icon) {
    return Column(
      children: [
        Icon(icon, color: widget.color, size: 20),
        const SizedBox(height: 4),
        Text(
          value,
          style: TextStyle(
            fontSize: 18,
            fontWeight: FontWeight.bold,
            color: widget.color,
          ),
        ),
        Text(label, style: const TextStyle(fontSize: 12, color: Colors.grey)),
      ],
    );
  }
}
