import 'dart:async';

import 'package:flutter/material.dart';

import '../services/health_api_service.dart';

/// Breathing Exercise Screen with realtime HR tracking
class BreathingExerciseScreen extends StatefulWidget {
  final String deviceId;

  const BreathingExerciseScreen({super.key, required this.deviceId});

  @override
  State<BreathingExerciseScreen> createState() =>
      _BreathingExerciseScreenState();
}

class _BreathingExerciseScreenState extends State<BreathingExerciseScreen>
    with SingleTickerProviderStateMixin {
  final HealthApiService _apiService = HealthApiService();

  late AnimationController _animationController;
  Timer? _pulseTimer;
  Timer? _hrMonitor;

  int _currentHR = 0;
  int _startingHR = 0;
  int _breathCount = 0;
  bool _isExercising = false;
  String _phase = 'Inhale'; // Inhale or Exhale
  int _duration = 5; // minutes
  int _secondsRemaining = 0;

  @override
  void initState() {
    super.initState();
    _animationController = AnimationController(
      vsync: this,
      duration: const Duration(seconds: 4), // 4s inhale
    );
    _loadCurrentHR();
  }

  @override
  void dispose() {
    _animationController.dispose();
    _pulseTimer?.cancel();
    _hrMonitor?.cancel();
    super.dispose();
  }

  Future<void> _loadCurrentHR() async {
    final vitals = await _apiService.getLatestVitals(widget.deviceId);
    if (vitals != null && mounted) {
      setState(() {
        _currentHR = vitals.heartRate;
        _startingHR = vitals.heartRate;
      });
    }
  }

  void _startExercise() {
    setState(() {
      _isExercising = true;
      _breathCount = 0;
      _secondsRemaining = _duration * 60;
      _startingHR = _currentHR;
    });

    // Breathing cycle: 4s inhale + 4s exhale = 8s/breath
    _pulseTimer = Timer.periodic(const Duration(seconds: 4), (timer) {
      if (mounted) {
        setState(() {
          _phase = _phase == 'Inhale' ? 'Exhale' : 'Inhale';

          if (_phase == 'Inhale') {
            _breathCount++;
            _animationController.forward(from: 0);
          } else {
            _animationController.reverse(from: 1);
          }
        });
      }
    });

    // Monitor HR every 5 seconds
    _hrMonitor = Timer.periodic(const Duration(seconds: 5), (timer) async {
      final vitals = await _apiService.getLatestVitals(widget.deviceId);
      if (vitals != null && mounted) {
        setState(() => _currentHR = vitals.heartRate);
      }
    });

    // Countdown timer
    Timer.periodic(const Duration(seconds: 1), (timer) {
      if (!_isExercising || _secondsRemaining <= 0) {
        timer.cancel();
        if (mounted && _secondsRemaining <= 0) {
          _stopExercise();
        }
        return;
      }

      if (mounted) {
        setState(() => _secondsRemaining--);
      }
    });

    _animationController.forward();
  }

  void _stopExercise() {
    _pulseTimer?.cancel();
    _hrMonitor?.cancel();
    _animationController.stop();

    setState(() => _isExercising = false);

    // Show results
    showDialog(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Exercise Complete!'),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Text('Breaths: $_breathCount'),
            Text('Starting HR: $_startingHR BPM'),
            Text('Current HR: $_currentHR BPM'),
            Text(
              'HR Change: ${_currentHR - _startingHR > 0 ? "+" : ""}${_currentHR - _startingHR} BPM',
            ),
            const SizedBox(height: 8),
            _currentHR < _startingHR
                ? const Text(
                    '✅ Great! Your heart rate decreased.',
                    style: TextStyle(color: Colors.green),
                  )
                : const Text(
                    '💡 Try longer, slower breaths next time.',
                    style: TextStyle(color: Colors.orange),
                  ),
          ],
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('OK'),
          ),
        ],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Breathing Exercise'),
        actions: [
          if (_isExercising)
            Center(
              child: Padding(
                padding: const EdgeInsets.only(right: 16),
                child: Text(
                  '${_secondsRemaining ~/ 60}:${(_secondsRemaining % 60).toString().padLeft(2, "0")}',
                  style: const TextStyle(fontSize: 18),
                ),
              ),
            ),
        ],
      ),
      body: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          children: [
            // Current HR display
            Card(
              color: Colors.blue.shade50,
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.spaceAround,
                  children: [
                    Column(
                      children: [
                        const Icon(Icons.favorite, color: Colors.red),
                        const SizedBox(height: 8),
                        Text(
                          '$_currentHR',
                          style: const TextStyle(
                            fontSize: 32,
                            fontWeight: FontWeight.bold,
                          ),
                        ),
                        const Text('Current HR'),
                      ],
                    ),
                    if (_isExercising)
                      Column(
                        children: [
                          const Icon(Icons.air, color: Colors.blue),
                          const SizedBox(height: 8),
                          Text(
                            '$_breathCount',
                            style: const TextStyle(
                              fontSize: 32,
                              fontWeight: FontWeight.bold,
                            ),
                          ),
                          const Text('Breaths'),
                        ],
                      ),
                  ],
                ),
              ),
            ),

            const SizedBox(height: 40),

            // Breathing animation
            Expanded(
              child: Center(
                child: AnimatedBuilder(
                  animation: _animationController,
                  builder: (context, child) {
                    return Container(
                      width: 100 + (_animationController.value * 150),
                      height: 100 + (_animationController.value * 150),
                      decoration: BoxDecoration(
                        shape: BoxShape.circle,
                        gradient: RadialGradient(
                          colors: [Colors.blue.shade300, Colors.blue.shade600],
                        ),
                        boxShadow: [
                          BoxShadow(
                            color: Colors.blue.withOpacity(0.5),
                            blurRadius: 30 + (_animationController.value * 50),
                            spreadRadius:
                                10 + (_animationController.value * 20),
                          ),
                        ],
                      ),
                      child: Center(
                        child: Text(
                          _phase,
                          style: TextStyle(
                            color: Colors.white,
                            fontSize: 24 + (_animationController.value * 8),
                            fontWeight: FontWeight.bold,
                          ),
                        ),
                      ),
                    );
                  },
                ),
              ),
            ),

            const SizedBox(height: 40),

            // Duration selector
            if (!_isExercising) ...[
              const Text('Duration:', style: TextStyle(fontSize: 16)),
              Slider(
                value: _duration.toDouble(),
                min: 1,
                max: 10,
                divisions: 9,
                label: '$_duration min',
                onChanged: (value) {
                  setState(() => _duration = value.toInt());
                },
              ),
              Text(
                '$_duration minutes',
                style: const TextStyle(
                  fontSize: 20,
                  fontWeight: FontWeight.bold,
                ),
              ),
            ],

            const SizedBox(height: 20),

            // Start/Stop button
            FilledButton.icon(
              onPressed: _isExercising ? _stopExercise : _startExercise,
              icon: Icon(_isExercising ? Icons.stop : Icons.play_arrow),
              label: Text(_isExercising ? 'Stop Exercise' : 'Start Exercise'),
              style: FilledButton.styleFrom(
                backgroundColor: _isExercising ? Colors.red : Colors.blue,
                padding: const EdgeInsets.symmetric(
                  horizontal: 40,
                  vertical: 16,
                ),
              ),
            ),

            const SizedBox(height: 20),

            // Instructions
            if (!_isExercising)
              const Text(
                'Follow the breathing circle:\nInhale for 4 seconds\nExhale for 4 seconds',
                textAlign: TextAlign.center,
                style: TextStyle(color: Colors.grey),
              ),
          ],
        ),
      ),
    );
  }
}
