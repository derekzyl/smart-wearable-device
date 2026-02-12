import 'package:flutter/material.dart';

import '../models/vitals_model.dart';

/// Collapsible alert section widget
class CollapsibleAlertsSection extends StatefulWidget {
  final List<HealthAlert> alerts;
  final VoidCallback? onDismiss;

  const CollapsibleAlertsSection({
    super.key,
    required this.alerts,
    this.onDismiss,
  });

  @override
  State<CollapsibleAlertsSection> createState() =>
      _CollapsibleAlertsSectionState();
}

class _CollapsibleAlertsSectionState extends State<CollapsibleAlertsSection> {
  bool _isExpanded = false;

  @override
  Widget build(BuildContext context) {
    if (widget.alerts.isEmpty) return const SizedBox.shrink();

    // Get latest 2 alerts for header
    final headerAlerts = widget.alerts.take(2).toList();
    final remainingAlerts = widget.alerts.length > 2
        ? widget.alerts.skip(2).toList()
        : <HealthAlert>[];

    return Card(
      elevation: 4,
      margin: const EdgeInsets.symmetric(vertical: 8),
      child: Column(
        children: [
          // Header with 2 latest alerts
          InkWell(
            onTap: remainingAlerts.isNotEmpty
                ? () => setState(() => _isExpanded = !_isExpanded)
                : null,
            child: Container(
              padding: const EdgeInsets.all(16),
              decoration: BoxDecoration(
                color: _getSeverityColor(
                  widget.alerts.first.severity,
                ).withOpacity(0.1),
                borderRadius: BorderRadius.circular(12),
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    children: [
                      Icon(
                        _getSeverityIcon(widget.alerts.first.severity),
                        color: _getSeverityColor(widget.alerts.first.severity),
                      ),
                      const SizedBox(width: 8),
                      Expanded(
                        child: Text(
                          'ALERTS (${widget.alerts.length})',
                          style: TextStyle(
                            fontWeight: FontWeight.bold,
                            fontSize: 16,
                            color: _getSeverityColor(
                              widget.alerts.first.severity,
                            ),
                          ),
                        ),
                      ),
                      if (remainingAlerts.isNotEmpty)
                        Icon(
                          _isExpanded ? Icons.expand_less : Icons.expand_more,
                          color: _getSeverityColor(
                            widget.alerts.first.severity,
                          ),
                        ),
                    ],
                  ),
                  const SizedBox(height: 12),
                  // Latest 2 alerts
                  ...headerAlerts.map((alert) => _buildAlertItem(alert)),
                  if (remainingAlerts.isNotEmpty && !_isExpanded)
                    Padding(
                      padding: const EdgeInsets.only(top: 8),
                      child: Text(
                        'Tap to see ${remainingAlerts.length} more alert${remainingAlerts.length > 1 ? 's' : ''}',
                        style: TextStyle(
                          fontSize: 12,
                          fontStyle: FontStyle.italic,
                          color: Colors.grey[600],
                        ),
                      ),
                    ),
                ],
              ),
            ),
          ),

          // Expandable section for remaining alerts
          if (_isExpanded && remainingAlerts.isNotEmpty)
            Container(
              padding: const EdgeInsets.all(16),
              decoration: BoxDecoration(
                color: Colors.grey[50],
                borderRadius: const BorderRadius.only(
                  bottomLeft: Radius.circular(12),
                  bottomRight: Radius.circular(12),
                ),
              ),
              child: Column(
                children: remainingAlerts
                    .map((alert) => _buildAlertItem(alert))
                    .toList(),
              ),
            ),
        ],
      ),
    );
  }

  Widget _buildAlertItem(HealthAlert alert) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 8),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(
            _getSeverityIcon(alert.severity),
            size: 16,
            color: _getSeverityColor(alert.severity),
          ),
          const SizedBox(width: 8),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  alert.message,
                  style: TextStyle(
                    color: _getSeverityColor(alert.severity),
                    fontWeight: FontWeight.w600,
                  ),
                ),
                Text(
                  _formatTime(alert.timestamp),
                  style: TextStyle(fontSize: 11, color: Colors.grey[600]),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Color _getSeverityColor(String severity) {
    switch (severity.toUpperCase()) {
      case 'CRITICAL':
        return Colors.red;
      case 'WARNING':
        return Colors.orange;
      case 'INFO':
        return Colors.blue;
      default:
        return Colors.grey;
    }
  }

  IconData _getSeverityIcon(String severity) {
    switch (severity.toUpperCase()) {
      case 'CRITICAL':
        return Icons.error;
      case 'WARNING':
        return Icons.warning;
      case 'INFO':
        return Icons.info;
      default:
        return Icons.notifications;
    }
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
