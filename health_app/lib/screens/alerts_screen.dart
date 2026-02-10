import 'package:flutter/material.dart';
import 'package:intl/intl.dart';

import '../models/vitals_model.dart';
import '../services/health_api_service.dart';

class AlertsScreen extends StatefulWidget {
  final String deviceId;

  const AlertsScreen({super.key, required this.deviceId});

  @override
  State<AlertsScreen> createState() => _AlertsScreenState();
}

class _AlertsScreenState extends State<AlertsScreen> {
  final HealthApiService _apiService = HealthApiService();
  List<HealthAlert> _alerts = [];
  bool _isLoading = true;
  bool _showOnlyCritical = false;

  @override
  void initState() {
    super.initState();
    _loadAlerts();
  }

  Future<void> _loadAlerts() async {
    setState(() => _isLoading = true);

    final alerts = _showOnlyCritical
        ? await _apiService.getCriticalAlerts(widget.deviceId)
        : await _apiService.getAlerts(widget.deviceId);

    if (mounted) {
      setState(() {
        _alerts = alerts;
        _isLoading = false;
      });
    }
  }

  Future<void> _acknowledgeAlert(int alertId) async {
    final success = await _apiService.acknowledgeAlert(alertId);

    if (success) {
      setState(() {
        _alerts.removeWhere((alert) => alert.id == alertId);
      });

      if (mounted) {
        ScaffoldMessenger.of(
          context,
        ).showSnackBar(const SnackBar(content: Text('Alert acknowledged')));
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Health Alerts'),
        actions: [
          FilterChip(
            label: const Text('Critical Only'),
            selected: _showOnlyCritical,
            onSelected: (selected) {
              setState(() => _showOnlyCritical = selected);
              _loadAlerts();
            },
          ),
          const SizedBox(width: 8),
          IconButton(icon: const Icon(Icons.refresh), onPressed: _loadAlerts),
        ],
      ),
      body: _isLoading
          ? const Center(child: CircularProgressIndicator())
          : _alerts.isEmpty
          ? Center(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  Icon(
                    Icons.check_circle_outline,
                    size: 64,
                    color: Colors.green.shade300,
                  ),
                  const SizedBox(height: 16),
                  Text(
                    'No alerts',
                    style: Theme.of(context).textTheme.headlineSmall,
                  ),
                  const SizedBox(height: 8),
                  Text(
                    'Your vitals are looking good!',
                    style: Theme.of(
                      context,
                    ).textTheme.bodyMedium?.copyWith(color: Colors.grey),
                  ),
                ],
              ),
            )
          : RefreshIndicator(
              onRefresh: _loadAlerts,
              child: ListView.builder(
                padding: const EdgeInsets.all(16),
                itemCount: _alerts.length,
                itemBuilder: (context, index) {
                  final alert = _alerts[index];
                  return _buildAlertCard(alert);
                },
              ),
            ),
    );
  }

  Widget _buildAlertCard(HealthAlert alert) {
    final color = alert.isCritical
        ? Colors.red
        : alert.isWarning
        ? Colors.orange
        : Colors.blue;

    final icon = alert.isCritical
        ? Icons.error
        : alert.isWarning
        ? Icons.warning
        : Icons.info;

    return Card(
      margin: const EdgeInsets.only(bottom: 12),
      elevation: alert.isCritical ? 4 : 2,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(12),
        side: BorderSide(
          color: alert.isCritical ? color : Colors.transparent,
          width: 2,
        ),
      ),
      child: Dismissible(
        key: Key(alert.id.toString()),
        direction: DismissDirection.endToStart,
        confirmDismiss: (direction) async {
          return await showDialog(
            context: context,
            builder: (context) => AlertDialog(
              title: const Text('Acknowledge Alert'),
              content: const Text('Mark this alert as acknowledged?'),
              actions: [
                TextButton(
                  onPressed: () => Navigator.pop(context, false),
                  child: const Text('Cancel'),
                ),
                FilledButton(
                  onPressed: () => Navigator.pop(context, true),
                  child: const Text('Acknowledge'),
                ),
              ],
            ),
          );
        },
        onDismissed: (_) => _acknowledgeAlert(alert.id),
        background: Container(
          alignment: Alignment.centerRight,
          padding: const EdgeInsets.only(right: 20),
          decoration: BoxDecoration(
            color: Colors.green,
            borderRadius: BorderRadius.circular(12),
          ),
          child: const Icon(Icons.check, color: Colors.white),
        ),
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Container(
                    padding: const EdgeInsets.all(8),
                    decoration: BoxDecoration(
                      color: color.withOpacity(0.1),
                      borderRadius: BorderRadius.circular(8),
                    ),
                    child: Icon(icon, color: color, size: 24),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Row(
                          children: [
                            Container(
                              padding: const EdgeInsets.symmetric(
                                horizontal: 8,
                                vertical: 4,
                              ),
                              decoration: BoxDecoration(
                                color: color,
                                borderRadius: BorderRadius.circular(4),
                              ),
                              child: Text(
                                alert.severity,
                                style: const TextStyle(
                                  color: Colors.white,
                                  fontSize: 10,
                                  fontWeight: FontWeight.bold,
                                ),
                              ),
                            ),
                            const SizedBox(width: 8),
                            Text(
                              alert.alertType
                                  .replaceAll('_', ' ')
                                  .toUpperCase(),
                              style: TextStyle(
                                fontSize: 12,
                                color: Colors.grey.shade600,
                                fontWeight: FontWeight.w500,
                              ),
                            ),
                          ],
                        ),
                        const SizedBox(height: 4),
                        Text(
                          DateFormat('MMM d, h:mm a').format(alert.timestamp),
                          style: TextStyle(
                            fontSize: 12,
                            color: Colors.grey.shade500,
                          ),
                        ),
                      ],
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 12),
              Text(
                alert.message,
                style: const TextStyle(fontSize: 15, height: 1.4),
              ),
              const SizedBox(height: 12),
              Row(
                children: [
                  Icon(
                    Icons.access_time,
                    size: 14,
                    color: Colors.grey.shade500,
                  ),
                  const SizedBox(width: 4),
                  Text(
                    _getTimeAgo(alert.timestamp),
                    style: TextStyle(fontSize: 12, color: Colors.grey.shade600),
                  ),
                  const Spacer(),
                  TextButton.icon(
                    onPressed: () => _acknowledgeAlert(alert.id),
                    icon: const Icon(Icons.check, size: 16),
                    label: const Text('Acknowledge'),
                    style: TextButton.styleFrom(foregroundColor: Colors.green),
                  ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }

  String _getTimeAgo(DateTime time) {
    final diff = DateTime.now().difference(time);

    if (diff.inSeconds < 60) return '${diff.inSeconds}s ago';
    if (diff.inMinutes < 60) return '${diff.inMinutes}m ago';
    if (diff.inHours < 24) return '${diff.inHours}h ago';
    if (diff.inDays < 7) return '${diff.inDays}d ago';
    return DateFormat('MMM d').format(time);
  }
}
