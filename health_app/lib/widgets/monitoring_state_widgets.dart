import 'package:flutter/material.dart';

/// Monitoring states for the device
enum MonitoringState { idle, monitoring, paused }

/// Floating Action Button for device state control
class MonitoringStateFAB extends StatelessWidget {
  final MonitoringState currentState;
  final VoidCallback onPressed;
  final bool isLoading;

  const MonitoringStateFAB({
    super.key,
    required this.currentState,
    required this.onPressed,
    this.isLoading = false,
  });

  @override
  Widget build(BuildContext context) {
    return FloatingActionButton.extended(
      onPressed: isLoading ? null : onPressed,
      icon: isLoading
          ? const SizedBox(
              width: 20,
              height: 20,
              child: CircularProgressIndicator(
                strokeWidth: 2,
                color: Colors.white,
              ),
            )
          : Icon(_getIcon()),
      label: Text(_getLabel()),
      backgroundColor: _getColor(),
      foregroundColor: Colors.white,
      elevation: 4,
    );
  }

  IconData _getIcon() {
    switch (currentState) {
      case MonitoringState.idle:
        return Icons.play_arrow;
      case MonitoringState.monitoring:
        return Icons.pause;
      case MonitoringState.paused:
        return Icons.play_arrow; // Resume
    }
  }

  String _getLabel() {
    switch (currentState) {
      case MonitoringState.idle:
        return 'START';
      case MonitoringState.monitoring:
        return 'PAUSE';
      case MonitoringState.paused:
        return 'RESUME';
    }
  }

  Color _getColor() {
    switch (currentState) {
      case MonitoringState.idle:
        return Colors.green;
      case MonitoringState.monitoring:
        return Colors.orange;
      case MonitoringState.paused:
        return Colors.blue; // Resume color
    }
  }
}

/// State indicator chip for app bar
class StateIndicatorChip extends StatelessWidget {
  final MonitoringState state;

  const StateIndicatorChip({super.key, required this.state});

  @override
  Widget build(BuildContext context) {
    return Chip(
      avatar: CircleAvatar(
        backgroundColor: _getColor(),
        child: Icon(_getIcon(), size: 16, color: Colors.white),
      ),
      label: Text(
        _getLabel(),
        style: const TextStyle(fontSize: 12, fontWeight: FontWeight.bold),
      ),
      backgroundColor: _getColor().withOpacity(0.1),
      side: BorderSide(color: _getColor(), width: 1),
      padding: const EdgeInsets.symmetric(horizontal: 4),
    );
  }

  IconData _getIcon() {
    switch (state) {
      case MonitoringState.idle:
        return Icons.hourglass_empty;
      case MonitoringState.monitoring:
        return Icons.sensors;
      case MonitoringState.paused:
        return Icons.pause_circle_outline;
    }
  }

  String _getLabel() {
    switch (state) {
      case MonitoringState.idle:
        return 'IDLE';
      case MonitoringState.monitoring:
        return 'MONITORING';
      case MonitoringState.paused:
        return 'PAUSED';
    }
  }

  Color _getColor() {
    switch (state) {
      case MonitoringState.idle:
        return Colors.grey;
      case MonitoringState.monitoring:
        return Colors.green;
      case MonitoringState.paused:
        return Colors.orange;
    }
  }
}

/// Mini FAB for stopping monitoring
class StopMonitorFAB extends StatelessWidget {
  final VoidCallback onPressed;
  final bool isLoading;

  const StopMonitorFAB({
    super.key,
    required this.onPressed,
    this.isLoading = false,
  });

  @override
  Widget build(BuildContext context) {
    return FloatingActionButton.small(
      onPressed: isLoading ? null : onPressed,
      backgroundColor: Colors.red,
      foregroundColor: Colors.white,
      heroTag: 'stop_fab',
      child: isLoading
          ? const SizedBox(
              width: 16,
              height: 16,
              child: CircularProgressIndicator(
                strokeWidth: 2,
                color: Colors.white,
              ),
            )
          : const Icon(Icons.stop),
    );
  }
}
