import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../services/ble_service.dart';
import 'dashboard_screen.dart';

class DeviceScanScreen extends StatefulWidget {
  const DeviceScanScreen({super.key});

  @override
  State<DeviceScanScreen> createState() => _DeviceScanScreenState();
}

class _DeviceScanScreenState extends State<DeviceScanScreen> {
  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      context.read<BLEService>().startScan();
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Scan for Health Watch'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      body: Consumer<BLEService>(
        builder: (context, bleService, child) {
          if (bleService.isScanning && bleService.devices.isEmpty) {
            return const Center(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  CircularProgressIndicator(),
                  SizedBox(height: 16),
                  Text('Scanning for devices...'),
                ],
              ),
            );
          }

          if (bleService.devices.isEmpty) {
            return Center(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  const Icon(Icons.bluetooth_searching, size: 64),
                  const SizedBox(height: 16),
                  const Text('No devices found'),
                  const SizedBox(height: 16),
                  ElevatedButton(
                    onPressed: () => bleService.startScan(),
                    child: const Text('Scan Again'),
                  ),
                ],
              ),
            );
          }

          return ListView.builder(
            itemCount: bleService.devices.length,
            itemBuilder: (context, index) {
              final device = bleService.devices[index];
              return ListTile(
                leading: const Icon(Icons.watch),
                title: Text(device.platformName.isNotEmpty 
                    ? device.platformName 
                    : 'Unknown Device'),
                subtitle: Text(device.remoteId.toString()),
                trailing: bleService.isConnected
                    ? const Icon(Icons.check_circle, color: Colors.green)
                    : const Icon(Icons.chevron_right),
                onTap: () async {
                  try {
                    await bleService.connect(device);
                    if (bleService.isConnected) {
                      Navigator.pushReplacement(
                        context,
                        MaterialPageRoute(
                          builder: (context) => const DashboardScreen(),
                        ),
                      );
                    }
                  } catch (e) {
                    ScaffoldMessenger.of(context).showSnackBar(
                      SnackBar(content: Text('Connection failed: $e')),
                    );
                  }
                },
              );
            },
          );
        },
      ),
      floatingActionButton: Consumer<BLEService>(
        builder: (context, bleService, child) {
          return FloatingActionButton(
            onPressed: bleService.isScanning 
                ? null 
                : () => bleService.startScan(),
            child: const Icon(Icons.refresh),
          );
        },
      ),
    );
  }
}


