import 'dart:async';

import 'package:flutter/material.dart';
import 'package:health_app/widgets/collapsible_alerts.dart';
import 'package:health_app/widgets/vital_history_modal.dart';

import '../config/api_config.dart';
import '../models/vitals_model.dart';
import '../services/health_api_service.dart';
import '../widgets/derived_metrics_widgets.dart';
import '../widgets/monitoring_state_widgets.dart';

class DashboardScreen extends StatefulWidget {
  final String deviceId;

  const DashboardScreen({super.key, required this.deviceId});

  @override
  State<DashboardScreen> createState() => _DashboardScreenState();
}

class _DashboardScreenState extends State<DashboardScreen> {
  final HealthApiService _apiService = HealthApiService();
  VitalSigns? _latestVitals;
  List<HealthAlert> _criticalAlerts = [];
  bool _isLoading = true;
  String? _error;
  Timer? _refreshTimer;

  // Device state control
  MonitoringState _deviceState = MonitoringState.idle;
  bool _isTogglingState = false;

  // Recent HR history for HRV calculation
  final List<int> _recentHRs = [];

  @override
  void initState() {
    super.initState();
    _loadData();

    // Auto-refresh every 5 seconds
    _refreshTimer = Timer.periodic(ApiConfig.vitalsRefreshInterval, (_) {
      _loadData();
    });
  }

  @override
  void dispose() {
    _refreshTimer?.cancel();
    super.dispose();
  }

  Future<void> _loadData() async {
    try {
      final vitals = await _apiService.getLatestVitals(widget.deviceId);
      final alerts = await _apiService.getCriticalAlerts(widget.deviceId);

      if (mounted) {
        setState(() {
          _latestVitals = vitals;
          _criticalAlerts = alerts;
          _isLoading = false;
          _error = null;

          // Update device state from vitals
          if (vitals != null) {
            _updateDeviceStateFromVitals(vitals);

            // Track recent HR for HRV
            if (vitals.heartRate > 0) {
              _recentHRs.add(vitals.heartRate);
              if (_recentHRs.length > 10) {
                _recentHRs.removeAt(0); // Keep last 10
              }
            }
          }
        });
      }
    } catch (e) {
      if (mounted) {
        setState(() {
          _error = e.toString();
          _isLoading = false;
        });
      }
    }
  }

  void _updateDeviceStateFromVitals(VitalSigns vitals) {
    // Parse monitoring state from system data
    final stateStr = vitals.monitoringState ?? 'idle';
    switch (stateStr.toLowerCase()) {
      case 'monitoring':
        _deviceState = MonitoringState.monitoring;
        break;
      case 'paused':
        _deviceState = MonitoringState.paused;
        break;
      default:
        _deviceState = MonitoringState.idle;
    }
  }

  Future<void> _toggleDeviceState() async {
    setState(() => _isTogglingState = true);

    String newState;
    switch (_deviceState) {
      case MonitoringState.idle:
        newState = 'monitoring';
        break;
      case MonitoringState.monitoring:
        newState = 'paused';
        break;
      case MonitoringState.paused:
        newState = 'monitoring'; // Resume
        break;
    }

    final success = await _apiService.setMonitoringState(
      widget.deviceId,
      newState,
    );

    _handleStateChangeResult(success, newState);
  }

  Future<void> _stopMonitoring() async {
    setState(() => _isTogglingState = true);

    final success = await _apiService.setMonitoringState(
      widget.deviceId,
      'idle',
    );

    _handleStateChangeResult(success, 'idle');
  }

  void _handleStateChangeResult(bool success, String newState) {
    if (mounted) {
      setState(() => _isTogglingState = false);

      if (success) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('Device state changed to ${newState.toUpperCase()}'),
            duration: const Duration(seconds: 2),
            backgroundColor: Colors.green,
          ),
        );
        // Refresh to get updated state from device
        Future.delayed(const Duration(milliseconds: 500), _loadData);
      } else {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text('Failed to change device state'),
            duration: Duration(seconds: 2),
            backgroundColor: Colors.red,
          ),
        );
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Health Monitor'),
        actions: [
          Padding(
            padding: const EdgeInsets.only(right: 8),
            child: StateIndicatorChip(state: _deviceState),
          ),
          IconButton(icon: const Icon(Icons.refresh), onPressed: _loadData),
        ],
      ),
      floatingActionButton: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          if (_deviceState != MonitoringState.idle) ...[
            StopMonitorFAB(
              onPressed: _stopMonitoring,
              isLoading: _isTogglingState,
            ),
            const SizedBox(width: 16),
          ],
          MonitoringStateFAB(
            currentState: _deviceState,
            onPressed: _toggleDeviceState,
            isLoading: _isTogglingState,
          ),
        ],
      ),
      body: _isLoading
          ? const Center(child: CircularProgressIndicator())
          : _error != null
          ? Center(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  const Icon(Icons.error_outline, size: 64, color: Colors.red),
                  const SizedBox(height: 16),
                  Text('Error: $_error'),
                  const SizedBox(height: 16),
                  ElevatedButton(
                    onPressed: _loadData,
                    child: const Text('Retry'),
                  ),
                ],
              ),
            )
          : RefreshIndicator(
              onRefresh: _loadData,
              child: ListView(
                padding: const EdgeInsets.all(16),
                children: [
                  // Vital Signs Cards (Hero Cards)
                  if (_latestVitals != null) ...[
                    Row(
                      children: [
                        Expanded(
                          child: _buildVitalCard(
                            title: 'Heart Rate',
                            value: '${_latestVitals!.heartRate}',
                            unit: 'BPM',
                            icon: Icons.favorite,
                            color: _getHRColor(_latestVitals!.heartRate),
                            quality: _latestVitals!.hrQuality,
                            vitalType: 'hr',
                          ),
                        ),
                        const SizedBox(width: 16),
                        Expanded(
                          child: _buildVitalCard(
                            title: 'SpO2',
                            value: '${_latestVitals!.spo2}',
                            unit: '%',
                            icon: Icons.water_drop,
                            color: _getSpO2Color(_latestVitals!.spo2),
                            quality: _latestVitals!.spo2Quality,
                            vitalType: 'spo2',
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 16),
                    Row(
                      children: [
                        Expanded(
                          child: _buildVitalCard(
                            title: 'Temperature',
                            value: _latestVitals!.temperature.toStringAsFixed(
                              1,
                            ),
                            unit: '°C',
                            icon: Icons.thermostat,
                            color: _getTempColor(_latestVitals!.temperature),
                            subtitle: _latestVitals!.tempEstimated
                                ? 'Estimated'
                                : _latestVitals!.tempSource,
                            vitalType: 'temp',
                          ),
                        ),
                        const SizedBox(width: 16),
                        Expanded(
                          child: _buildVitalCard(
                            title: 'Battery',
                            value: '${_latestVitals!.batteryPercent}',
                            unit: '%',
                            icon: _getBatteryIcon(
                              _latestVitals!.batteryPercent,
                            ),
                            color: _getBatteryColor(
                              _latestVitals!.batteryPercent,
                            ),
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 24),

                    // Derived Metrics Section
                    HealthScoreCard(
                      heartRate: _latestVitals!.heartRate,
                      spo2: _latestVitals!.spo2,
                      temperature: _latestVitals!.temperature,
                      hrQuality: _latestVitals!.hrQuality,
                      spo2Quality: _latestVitals!.spo2Quality,
                    ),
                    const SizedBox(height: 16),
                    DerivedMetricsRow(
                      heartRate: _latestVitals!.heartRate,
                      spo2: _latestVitals!.spo2,
                      recentHRs: _recentHRs,
                    ),

                    // Critical Alerts Section (Below Vitals)
                    if (_criticalAlerts.isNotEmpty) ...[
                      const SizedBox(height: 24),
                      CollapsibleAlertsSection(alerts: _criticalAlerts),
                    ],

                    const SizedBox(height: 24),

                    // Last Update Time
                    Center(
                      child: Text(
                        'Last updated: ${_formatTime(_latestVitals!.timestamp)}',
                        style: Theme.of(context).textTheme.bodySmall,
                      ),
                    ),
                  ] else ...[
                    const Center(
                      child: Padding(
                        padding: EdgeInsets.all(32),
                        child: Text('No data available yet'),
                      ),
                    ),
                  ],
                ],
              ),
            ),
    );
  }

  Widget _buildVitalCard({
    required String title,
    required String value,
    required String unit,
    required IconData icon,
    required Color color,
    int? quality,
    String? subtitle,
    String? vitalType, // Added for history modal
  }) {
    return InkWell(
      onTap: vitalType != null
          ? () {
              showModalBottomSheet(
                context: context,
                isScrollControlled: true,
                backgroundColor: Colors.transparent,
                builder: (context) => VitalHistoryModal(
                  deviceId: widget.deviceId,
                  vitalType: vitalType,
                  title: title,
                  color: color,
                ),
              );
            }
          : null,
      borderRadius: BorderRadius.circular(12),
      child: Card(
        elevation: 4,
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Icon(icon, color: color, size: 24),
                  const SizedBox(width: 8),
                  Text(
                    title,
                    style: const TextStyle(
                      fontSize: 14,
                      fontWeight: FontWeight.w500,
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 12),
              Row(
                crossAxisAlignment: CrossAxisAlignment.end,
                children: [
                  Text(
                    value,
                    style: TextStyle(
                      fontSize: 32,
                      fontWeight: FontWeight.bold,
                      color: color,
                    ),
                  ),
                  const SizedBox(width: 4),
                  Padding(
                    padding: const EdgeInsets.only(bottom: 6),
                    child: Text(
                      unit,
                      style: TextStyle(
                        fontSize: 16,
                        color: Colors.grey.shade600,
                      ),
                    ),
                  ),
                ],
              ),
              if (quality != null) ...[
                const SizedBox(height: 8),
                Row(
                  children: [
                    Icon(
                      Icons.signal_cellular_alt,
                      size: 14,
                      color: quality > 70 ? Colors.green : Colors.orange,
                    ),
                    const SizedBox(width: 4),
                    Text(
                      'Quality: $quality%',
                      style: TextStyle(
                        fontSize: 12,
                        color: Colors.grey.shade600,
                      ),
                    ),
                  ],
                ),
              ],
              if (subtitle != null) ...[
                const SizedBox(height: 4),
                Text(
                  subtitle,
                  style: TextStyle(
                    fontSize: 12,
                    color: Colors.grey.shade600,
                    fontStyle: FontStyle.italic,
                  ),
                ),
              ],
            ],
          ),
        ),
      ),
    );
  }

  Color _getHRColor(int hr) {
    if (hr == 0) return Colors.grey;
    if (hr < 50) return Colors.blue;
    if (hr > 100) return Colors.orange;
    return Colors.green;
  }

  Color _getSpO2Color(int spo2) {
    if (spo2 == 0) return Colors.grey;
    if (spo2 < 90) return Colors.red;
    if (spo2 < 95) return Colors.orange;
    return Colors.green;
  }

  Color _getTempColor(double temp) {
    if (temp > 38.0) return Colors.red;
    if (temp > 37.5) return Colors.orange;
    if (temp < 35.5) return Colors.blue;
    return Colors.green;
  }

  Color _getBatteryColor(int battery) {
    if (battery < 20) return Colors.red;
    if (battery < 50) return Colors.orange;
    return Colors.green;
  }

  IconData _getBatteryIcon(int battery) {
    if (battery < 20) return Icons.battery_0_bar;
    if (battery < 40) return Icons.battery_2_bar;
    if (battery < 60) return Icons.battery_4_bar;
    if (battery < 80) return Icons.battery_5_bar;
    return Icons.battery_full;
  }

  String _formatTime(DateTime time) {
    final now = DateTime.now();
    final diff = now.difference(time);

    if (diff.inSeconds < 60) return '${diff.inSeconds}s ago';
    if (diff.inMinutes < 60) return '${diff.inMinutes}m ago';
    if (diff.inHours < 24) return '${diff.inHours}h ago';
    return '${diff.inDays}d ago';
  }
}
