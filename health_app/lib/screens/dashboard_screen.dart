import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:intl/intl.dart';
import '../services/ble_service.dart';
import '../services/api_service.dart';
import '../widgets/vitals_card.dart';
import '../widgets/chart_widget.dart';

class DashboardScreen extends StatefulWidget {
  const DashboardScreen({super.key});

  @override
  State<DashboardScreen> createState() => _DashboardScreenState();
}

class _DashboardScreenState extends State<DashboardScreen> {
  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Health Dashboard'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
        actions: [
          IconButton(
            icon: const Icon(Icons.bluetooth_disabled),
            onPressed: () async {
              await context.read<BLEService>().disconnect();
              Navigator.pop(context);
            },
          ),
        ],
      ),
      body: Consumer<BLEService>(
        builder: (context, bleService, child) {
          final vitals = bleService.currentVitals;

          if (vitals == null) {
            return const Center(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  CircularProgressIndicator(),
                  SizedBox(height: 16),
                  Text('Waiting for data...'),
                ],
              ),
            );
          }

          return SingleChildScrollView(
            padding: const EdgeInsets.all(16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                // Current Vitals Grid
                Row(
                  children: [
                    Expanded(
                      child: VitalsCard(
                        title: 'Heart Rate',
                        value: '${vitals.heartRate.toInt()}',
                        unit: 'bpm',
                        icon: Icons.favorite,
                        color: Colors.red,
                      ),
                    ),
                    const SizedBox(width: 16),
                    Expanded(
                      child: VitalsCard(
                        title: 'SpO₂',
                        value: '${vitals.spo2.toInt()}',
                        unit: '%',
                        icon: Icons.air,
                        color: Colors.blue,
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 16),
                Row(
                  children: [
                    Expanded(
                      child: VitalsCard(
                        title: 'Temperature',
                        value: vitals.temperature.toStringAsFixed(1),
                        unit: '°C',
                        icon: Icons.thermostat,
                        color: Colors.orange,
                      ),
                    ),
                    const SizedBox(width: 16),
                    Expanded(
                      child: VitalsCard(
                        title: 'Steps',
                        value: '${vitals.steps}',
                        unit: '',
                        icon: Icons.directions_walk,
                        color: Colors.green,
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 24),
                
                // Charts Section
                const Text(
                  'Trends',
                  style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold),
                ),
                const SizedBox(height: 16),
                SizedBox(
                  height: 200,
                  child: ChartWidget(
                    title: 'Heart Rate',
                    data: [vitals.heartRate],
                  ),
                ),
                const SizedBox(height: 16),
                SizedBox(
                  height: 200,
                  child: ChartWidget(
                    title: 'SpO₂',
                    data: [vitals.spo2],
                  ),
                ),
                
                // Battery Level
                const SizedBox(height: 24),
                Card(
                  child: ListTile(
                    leading: Icon(
                      Icons.battery_charging_full,
                      color: vitals.batteryLevel > 20 ? Colors.green : Colors.red,
                    ),
                    title: const Text('Battery Level'),
                    trailing: Text(
                      '${vitals.batteryLevel.toInt()}%',
                      style: const TextStyle(
                        fontSize: 18,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                  ),
                ),
              ],
            ),
          );
        },
      ),
    );
  }
}


