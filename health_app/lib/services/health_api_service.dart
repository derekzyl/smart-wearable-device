import 'dart:convert';

import 'package:http/http.dart' as http;

import '../config/api_config.dart';
import '../models/vitals_model.dart';

/// API Service for Health Monitoring Backend
class HealthApiService {
  // Get latest vitals for a device
  Future<VitalSigns?> getLatestVitals(String deviceId) async {
    try {
      final url = Uri.parse(
        '${ApiConfig.baseUrl}${ApiConfig.vitalsEndpoint}/$deviceId/latest',
      );
      final response = await http.get(url).timeout(ApiConfig.timeout);

      if (response.statusCode == 200) {
        final data = jsonDecode(response.body);
        return VitalSigns.fromJson(data);
      } else if (response.statusCode == 404) {
        // No readings yet
        return null;
      } else {
        throw Exception('Failed to load vitals: ${response.statusCode}');
      }
    } catch (e) {
      print('Error fetching vitals: $e');
      return null;
    }
  }

  // Get historical vitals
  Future<List<VitalSigns>> getVitalsHistory(
    String deviceId, {
    DateTime? startDate,
    DateTime? endDate,
    int limit = 100,
  }) async {
    try {
      final queryParams = {
        'limit': limit.toString(),
        if (startDate != null) 'start_date': startDate.toIso8601String(),
        if (endDate != null) 'end_date': endDate.toIso8601String(),
      };

      final url = Uri.parse(
        '${ApiConfig.baseUrl}${ApiConfig.vitalsEndpoint}/$deviceId/history',
      ).replace(queryParameters: queryParams);

      final response = await http.get(url).timeout(ApiConfig.timeout);

      if (response.statusCode == 200) {
        final List<dynamic> data = jsonDecode(response.body);
        return data.map((json) => VitalSigns.fromJson(json)).toList();
      } else {
        throw Exception('Failed to load history: ${response.statusCode}');
      }
    } catch (e) {
      print('Error fetching history: $e');
      return [];
    }
  }

  // Get all alerts for device
  Future<List<HealthAlert>> getAlerts(String deviceId) async {
    try {
      final url = Uri.parse(
        '${ApiConfig.baseUrl}${ApiConfig.alertsEndpoint}/$deviceId',
      );
      final response = await http.get(url).timeout(ApiConfig.timeout);

      if (response.statusCode == 200) {
        final List<dynamic> data = jsonDecode(response.body);
        return data.map((json) => HealthAlert.fromJson(json)).toList();
      } else {
        throw Exception('Failed to load alerts: ${response.statusCode}');
      }
    } catch (e) {
      print('Error fetching alerts: $e');
      return [];
    }
  }

  // Get critical alerts only
  Future<List<HealthAlert>> getCriticalAlerts(String deviceId) async {
    try {
      final url = Uri.parse(
        '${ApiConfig.baseUrl}${ApiConfig.alertsEndpoint}/$deviceId/critical',
      );
      final response = await http.get(url).timeout(ApiConfig.timeout);

      if (response.statusCode == 200) {
        final List<dynamic> data = jsonDecode(response.body);
        return data.map((json) => HealthAlert.fromJson(json)).toList();
      } else {
        throw Exception(
          'Failed to load critical alerts: ${response.statusCode}',
        );
      }
    } catch (e) {
      print('Error fetching critical alerts: $e');
      return [];
    }
  }

  // Acknowledge alert
  Future<bool> acknowledgeAlert(int alertId) async {
    try {
      final url = Uri.parse(
        '${ApiConfig.baseUrl}${ApiConfig.alertsEndpoint}/$alertId/acknowledge',
      );
      final response = await http.put(url).timeout(ApiConfig.timeout);

      return response.statusCode == 200;
    } catch (e) {
      print('Error acknowledging alert: $e');
      return false;
    }
  }

  // Get device info
  Future<HealthDevice?> getDevice(String deviceId) async {
    try {
      final url = Uri.parse(
        '${ApiConfig.baseUrl}${ApiConfig.devicesEndpoint}/$deviceId',
      );
      final response = await http.get(url).timeout(ApiConfig.timeout);

      if (response.statusCode == 200) {
        final data = jsonDecode(response.body);
        return HealthDevice.fromJson(data);
      } else if (response.statusCode == 404) {
        return null;
      } else {
        throw Exception('Failed to load device: ${response.statusCode}');
      }
    } catch (e) {
      print('Error fetching device: $e');
      return null;
    }
  }

  // Set resting heart rate (calibration)
  Future<bool> setRestingHR(String deviceId, double restingHR) async {
    try {
      final url = Uri.parse(
        '${ApiConfig.baseUrl}${ApiConfig.devicesEndpoint}/$deviceId/calibrate',
      );
      final response = await http
          .post(
            url,
            headers: {'Content-Type': 'application/json'},
            body: jsonEncode({'resting_hr': restingHR}),
          )
          .timeout(ApiConfig.timeout);

      return response.statusCode == 200;
    } catch (e) {
      print('Error setting resting HR: $e');
      return false;
    }
  }

  // Get summary statistics
  Future<Map<String, dynamic>?> getSummaryStats(
    String deviceId, {
    String period = 'daily', // daily, weekly, monthly
  }) async {
    try {
      final url = Uri.parse(
        '${ApiConfig.baseUrl}${ApiConfig.analyticsEndpoint}/$deviceId/summary',
      ).replace(queryParameters: {'period': period});

      final response = await http.get(url).timeout(ApiConfig.timeout);

      if (response.statusCode == 200) {
        return jsonDecode(response.body);
      } else {
        throw Exception('Failed to load stats: ${response.statusCode}');
      }
    } catch (e) {
      print('Error fetching stats: $e');
      return null;
    }
  }

  // Get health correlation analysis
  Future<Map<String, dynamic>?> getCorrelationAnalysis(
    String deviceId, {
    int hours = 24,
  }) async {
    try {
      final url = Uri.parse(
        '${ApiConfig.baseUrl}${ApiConfig.analyticsEndpoint}/$deviceId/correlation',
      ).replace(queryParameters: {'hours': hours.toString()});

      final response = await http.get(url).timeout(ApiConfig.timeout);

      if (response.statusCode == 200) {
        return jsonDecode(response.body);
      } else {
        throw Exception('Failed to load correlation: ${response.statusCode}');
      }
    } catch (e) {
      print('Error fetching correlation: $e');
      return null;
    }
  }

  // Register new device
  Future<HealthDevice?> registerDevice({
    required String deviceId,
    String? userName,
    int? age,
    String? gender,
  }) async {
    try {
      final url = Uri.parse('${ApiConfig.baseUrl}${ApiConfig.devicesEndpoint}');
      final response = await http
          .post(
            url,
            headers: {'Content-Type': 'application/json'},
            body: jsonEncode({
              'device_id': deviceId,
              if (userName != null) 'user_name': userName,
              if (age != null) 'age': age,
              if (gender != null) 'gender': gender,
            }),
          )
          .timeout(ApiConfig.timeout);

      if (response.statusCode == 200) {
        final data = jsonDecode(response.body);
        return HealthDevice.fromJson(data);
      } else {
        throw Exception('Failed to register device: ${response.statusCode}');
      }
    } catch (e) {
      print('Error registering device: $e');
      return null;
    }
  }
}
