import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../models/vitals_data.dart';

class BLEService extends ChangeNotifier {
  static const String SERVICE_UUID = "12345678-1234-1234-1234-123456789abc";
  static const String CHAR_HEART_RATE_UUID = "00002a37-0000-1000-8000-00805f9b34fb";
  static const String CHAR_SPO2_UUID = "12345678-1234-1234-1234-123456789abd";
  static const String CHAR_TEMPERATURE_UUID = "12345678-1234-1234-1234-123456789abe";
  static const String CHAR_IMU_UUID = "12345678-1234-1234-1234-123456789abf";
  static const String CHAR_BATTERY_UUID = "00002a19-0000-1000-8000-00805f9b34fb";

  BluetoothDevice? _connectedDevice;
  BluetoothCharacteristic? _heartRateChar;
  BluetoothCharacteristic? _spo2Char;
  BluetoothCharacteristic? _temperatureChar;
  BluetoothCharacteristic? _imuChar;
  BluetoothCharacteristic? _batteryChar;

  bool _isScanning = false;
  bool _isConnected = false;
  List<BluetoothDevice> _devices = [];
  StreamSubscription? _subscription;

  VitalsData? _currentVitals;
  IMUData? _currentIMU;

  bool get isScanning => _isScanning;
  bool get isConnected => _isConnected;
  List<BluetoothDevice> get devices => _devices;
  VitalsData? get currentVitals => _currentVitals;
  IMUData? get currentIMU => _currentIMU;

  Future<bool> checkBluetooth() async {
    try {
      BluetoothAdapterState state = await FlutterBluePlus.adapterState.first;
      if (state != BluetoothAdapterState.on) {
        await FlutterBluePlus.turnOn();
      }
      return true;
    } catch (e) {
      return false;
    }
  }

  Future<void> startScan() async {
    if (_isScanning) return;

    _isScanning = true;
    _devices.clear();
    notifyListeners();

    await checkBluetooth();

    FlutterBluePlus.startScan(timeout: const Duration(seconds: 10));

    _subscription = FlutterBluePlus.scanResults.listen((results) {
      for (ScanResult result in results) {
        if (!_devices.contains(result.device) &&
            (result.device.platformName.isNotEmpty ||
                result.advertisementData.serviceUuids.contains(SERVICE_UUID))) {
          _devices.add(result.device);
          notifyListeners();
        }
      }
    });

    await Future.delayed(const Duration(seconds: 10));
    await stopScan();
  }

  Future<void> stopScan() async {
    if (!_isScanning) return;
    await FlutterBluePlus.stopScan();
    _isScanning = false;
    _subscription?.cancel();
    notifyListeners();
  }

  Future<void> connect(BluetoothDevice device) async {
    try {
      _connectedDevice = device;
      await device.connect(timeout: const Duration(seconds: 15));
      _isConnected = true;

      List<BluetoothService> services = await device.discoverServices();

      for (BluetoothService service in services) {
        if (service.uuid.toString().toLowerCase() == SERVICE_UUID.toLowerCase()) {
          for (BluetoothCharacteristic characteristic in service.characteristics) {
            String charUuid = characteristic.uuid.toString().toLowerCase();

            if (charUuid == CHAR_HEART_RATE_UUID.toLowerCase()) {
              _heartRateChar = characteristic;
              await characteristic.setNotifyValue(true);
              characteristic.onValueReceived.listen((value) {
                if (value.isNotEmpty) {
                  _currentVitals = VitalsData(
                    heartRate: value[0].toDouble(),
                    spo2: _currentVitals?.spo2 ?? 0.0,
                    temperature: _currentVitals?.temperature ?? 0.0,
                    steps: _currentVitals?.steps ?? 0,
                    batteryLevel: _currentVitals?.batteryLevel ?? 0.0,
                    timestamp: DateTime.now(),
                  );
                  notifyListeners();
                }
              });
            } else if (charUuid == CHAR_SPO2_UUID.toLowerCase()) {
              _spo2Char = characteristic;
              await characteristic.setNotifyValue(true);
              characteristic.onValueReceived.listen((value) {
                if (value.isNotEmpty && _currentVitals != null) {
                  _currentVitals = VitalsData(
                    heartRate: _currentVitals!.heartRate,
                    spo2: value[0].toDouble(),
                    temperature: _currentVitals!.temperature,
                    steps: _currentVitals!.steps,
                    batteryLevel: _currentVitals!.batteryLevel,
                    timestamp: DateTime.now(),
                  );
                  notifyListeners();
                }
              });
            } else if (charUuid == CHAR_TEMPERATURE_UUID.toLowerCase()) {
              _temperatureChar = characteristic;
              await characteristic.setNotifyValue(true);
              characteristic.onValueReceived.listen((value) {
                if (value.length >= 2 && _currentVitals != null) {
                  int tempRaw = (value[1] << 8) | value[0];
                  // Handle signed int16
                  if (tempRaw > 32767) tempRaw -= 65536;
                  double temperature = tempRaw / 100.0;
                  _currentVitals = VitalsData(
                    heartRate: _currentVitals!.heartRate,
                    spo2: _currentVitals!.spo2,
                    temperature: temperature,
                    steps: _currentVitals!.steps,
                    batteryLevel: _currentVitals!.batteryLevel,
                    timestamp: DateTime.now(),
                  );
                  notifyListeners();
                }
              });
            } else if (charUuid == CHAR_IMU_UUID.toLowerCase()) {
              _imuChar = characteristic;
              await characteristic.setNotifyValue(true);
              characteristic.onValueReceived.listen((value) {
                // Parse JSON from IMU data
                String jsonString = String.fromCharCodes(value);
                // TODO: Parse JSON and update _currentIMU
                notifyListeners();
              });
            } else if (charUuid == CHAR_BATTERY_UUID.toLowerCase()) {
              _batteryChar = characteristic;
              await characteristic.setNotifyValue(true);
              characteristic.onValueReceived.listen((value) {
                if (value.isNotEmpty && _currentVitals != null) {
                  _currentVitals = VitalsData(
                    heartRate: _currentVitals!.heartRate,
                    spo2: _currentVitals!.spo2,
                    temperature: _currentVitals!.temperature,
                    steps: _currentVitals!.steps,
                    batteryLevel: value[0].toDouble(),
                    timestamp: DateTime.now(),
                  );
                  notifyListeners();
                }
              });
            }
          }
        }
      }

      notifyListeners();
    } catch (e) {
      _isConnected = false;
      notifyListeners();
      rethrow;
    }
  }

  Future<void> disconnect() async {
    if (_connectedDevice != null) {
      await _connectedDevice!.disconnect();
      _connectedDevice = null;
      _isConnected = false;
      _currentVitals = null;
      _currentIMU = null;
      notifyListeners();
    }
  }

  @override
  void dispose() {
    stopScan();
    disconnect();
    _subscription?.cancel();
    super.dispose();
  }
}

