import 'dart:convert';

import 'package:http/http.dart' as http;

import '../config/api_config.dart';
import '../models/vitals_model.dart';

/// API Service for Health Monitoring Backend
class HealthApiService {
  // Debug logging helpers
  void _logRequest(
    String method,
    String url, {
    Map<String, String>? headers,
    String? body,
  }) {
    print('\n🔵 ═══ API REQUEST ═══');
    print('Method: $method');
    print('URL: $url');
    if (headers != null && headers.isNotEmpty) {
      print('Headers: $headers');
    }
    if (body != null) {
      print('Body: $body');
    }
    print('════════════════════\n');
  }

  void _logResponse(String method, String url, int statusCode, String body) {
    final icon = statusCode >= 200 && statusCode < 300 ? '🟢' : '🔴';
    print('\n$icon ═══ API RESPONSE ═══');
    print('Method: $method');
    print('URL: $url');
    print('Status: $statusCode');
    print('Body: $body');
    print('═════════════════════\n');
  }

  void _logError(String method, String url, dynamic error) {
    print('\n🔴 ═══ API ERROR ═══');
    print('Method: $method');
    print('URL: $url');
    print('Error: $error');
    print('═══════════════════\n');
  }

  // Get latest vitals for a device
  Future<VitalSigns?> getLatestVitals(String deviceId) async {
    final url = Uri.parse(
      '${ApiConfig.baseUrl}${ApiConfig.vitalsEndpoint}/$deviceId/latest',
    );

    try {
      _logRequest('GET', url.toString());
      final response = await http.get(url).timeout(ApiConfig.timeout);
      _logResponse('GET', url.toString(), response.statusCode, response.body);

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
      _logError('GET', url.toString(), e);
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
    final queryParams = {
      'limit': limit.toString(),
      if (startDate != null) 'start_date': startDate.toIso8601String(),
      if (endDate != null) 'end_date': endDate.toIso8601String(),
    };

    final url = Uri.parse(
      '${ApiConfig.baseUrl}${ApiConfig.vitalsEndpoint}/$deviceId/history',
    ).replace(queryParameters: queryParams);

    try {
      _logRequest('GET', url.toString());
      final response = await http.get(url).timeout(ApiConfig.timeout);
      _logResponse('GET', url.toString(), response.statusCode, response.body);

      if (response.statusCode == 200) {
        final List<dynamic> data = jsonDecode(response.body);
        return data.map((json) => VitalSigns.fromJson(json)).toList();
      } else {
        throw Exception('Failed to load history: ${response.statusCode}');
      }
    } catch (e) {
      _logError('GET', url.toString(), e);
      return [];
    }
  }

  // Get all alerts for device
  Future<List<HealthAlert>> getAlerts(String deviceId) async {
    final url = Uri.parse(
      '${ApiConfig.baseUrl}${ApiConfig.alertsEndpoint}/$deviceId',
    );

    try {
      _logRequest('GET', url.toString());
      final response = await http.get(url).timeout(ApiConfig.timeout);
      _logResponse('GET', url.toString(), response.statusCode, response.body);

      if (response.statusCode == 200) {
        final List<dynamic> data = jsonDecode(response.body);
        return data.map((json) => HealthAlert.fromJson(json)).toList();
      } else {
        throw Exception('Failed to load alerts: ${response.statusCode}');
      }
    } catch (e) {
      _logError('GET', url.toString(), e);
      return [];
    }
  }

  // Get critical alerts only
  Future<List<HealthAlert>> getCriticalAlerts(String deviceId) async {
    final url = Uri.parse(
      '${ApiConfig.baseUrl}${ApiConfig.alertsEndpoint}/$deviceId/critical',
    );

    try {
      _logRequest('GET', url.toString());
      final response = await http.get(url).timeout(ApiConfig.timeout);
      _logResponse('GET', url.toString(), response.statusCode, response.body);

      if (response.statusCode == 200) {
        final List<dynamic> data = jsonDecode(response.body);
        return data.map((json) => HealthAlert.fromJson(json)).toList();
      } else {
        throw Exception(
          'Failed to load critical alerts: ${response.statusCode}',
        );
      }
    } catch (e) {
      _logError('GET', url.toString(), e);
      return [];
    }
  }

  // Acknowledge alert
  Future<bool> acknowledgeAlert(int alertId) async {
    final url = Uri.parse(
      '${ApiConfig.baseUrl}${ApiConfig.alertsEndpoint}/$alertId/acknowledge',
    );

    try {
      _logRequest('PUT', url.toString());
      final response = await http.put(url).timeout(ApiConfig.timeout);
      _logResponse('PUT', url.toString(), response.statusCode, response.body);

      return response.statusCode == 200;
    } catch (e) {
      _logError('PUT', url.toString(), e);
      return false;
    }
  }

  // Get device info
  Future<HealthDevice?> getDevice(String deviceId) async {
    final url = Uri.parse(
      '${ApiConfig.baseUrl}${ApiConfig.devicesEndpoint}/$deviceId',
    );

    try {
      _logRequest('GET', url.toString());
      final response = await http.get(url).timeout(ApiConfig.timeout);
      _logResponse('GET', url.toString(), response.statusCode, response.body);

      if (response.statusCode == 200) {
        final data = jsonDecode(response.body);
        return HealthDevice.fromJson(data);
      } else if (response.statusCode == 404) {
        return null;
      } else {
        throw Exception('Failed to load device: ${response.statusCode}');
      }
    } catch (e) {
      _logError('GET', url.toString(), e);
      return null;
    }
  }

  // Set resting heart rate (calibration)
  Future<bool> setRestingHR(String deviceId, double restingHR) async {
    final url = Uri.parse(
      '${ApiConfig.baseUrl}${ApiConfig.devicesEndpoint}/$deviceId/calibrate',
    );
    final body = jsonEncode({'resting_hr': restingHR});

    try {
      _logRequest(
        'POST',
        url.toString(),
        body: body,
        headers: {'Content-Type': 'application/json'},
      );
      final response = await http
          .post(url, headers: {'Content-Type': 'application/json'}, body: body)
          .timeout(ApiConfig.timeout);
      _logResponse('POST', url.toString(), response.statusCode, response.body);

      return response.statusCode == 200;
    } catch (e) {
      _logError('POST', url.toString(), e);
      return false;
    }
  }

  // Get summary statistics
  Future<Map<String, dynamic>?> getSummaryStats(
    String deviceId, {
    String period = 'daily', // daily, weekly, monthly
  }) async {
    final url = Uri.parse(
      '${ApiConfig.baseUrl}${ApiConfig.analyticsEndpoint}/$deviceId/summary',
    ).replace(queryParameters: {'period': period});

    try {
      _logRequest('GET', url.toString());
      final response = await http.get(url).timeout(ApiConfig.timeout);
      _logResponse('GET', url.toString(), response.statusCode, response.body);

      if (response.statusCode == 200) {
        return jsonDecode(response.body);
      } else {
        throw Exception('Failed to load stats: ${response.statusCode}');
      }
    } catch (e) {
      _logError('GET', url.toString(), e);
      return null;
    }
  }

  // Get health correlation analysis
  Future<Map<String, dynamic>?> getCorrelationAnalysis(
    String deviceId, {
    int hours = 24,
  }) async {
    final url = Uri.parse(
      '${ApiConfig.baseUrl}${ApiConfig.analyticsEndpoint}/$deviceId/correlation',
    ).replace(queryParameters: {'hours': hours.toString()});

    try {
      _logRequest('GET', url.toString());
      final response = await http.get(url).timeout(ApiConfig.timeout);
      _logResponse('GET', url.toString(), response.statusCode, response.body);

      if (response.statusCode == 200) {
        return jsonDecode(response.body);
      } else {
        throw Exception('Failed to load correlation: ${response.statusCode}');
      }
    } catch (e) {
      _logError('GET', url.toString(), e);
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
    final url = Uri.parse('${ApiConfig.baseUrl}${ApiConfig.devicesEndpoint}');
    final body = jsonEncode({
      'device_id': deviceId,
      if (userName != null) 'user_name': userName,
      if (age != null) 'age': age,
      if (gender != null) 'gender': gender,
    });

    try {
      _logRequest(
        'POST',
        url.toString(),
        body: body,
        headers: {'Content-Type': 'application/json'},
      );
      final response = await http
          .post(url, headers: {'Content-Type': 'application/json'}, body: body)
          .timeout(ApiConfig.timeout);
      _logResponse('POST', url.toString(), response.statusCode, response.body);

      if (response.statusCode == 200) {
        final data = jsonDecode(response.body);
        return HealthDevice.fromJson(data);
      } else {
        throw Exception('Failed to register device: ${response.statusCode}');
      }
    } catch (e) {
      _logError('POST', url.toString(), e);
      return null;
    }
  }

  // ==================== DEVICE STATE CONTROL ====================

  /// Set monitoring state (idle, monitoring, paused)
  /// Device will poll and update its state accordingly
  Future<bool> setMonitoringState(String deviceId, String state) async {
    final url = Uri.parse(
      '${ApiConfig.baseUrl}${ApiConfig.devicesEndpoint}/$deviceId/state',
    );
    final body = jsonEncode({'state': state});

    try {
      _logRequest(
        'POST',
        url.toString(),
        body: body,
        headers: {'Content-Type': 'application/json'},
      );
      final response = await http
          .post(url, headers: {'Content-Type': 'application/json'}, body: body)
          .timeout(ApiConfig.timeout);
      _logResponse('POST', url.toString(), response.statusCode, response.body);

      return response.statusCode == 200;
    } catch (e) {
      _logError('POST', url.toString(), e);
      return false;
    }
  }

  /// Get pending state commands for device (polled by device)
  Future<Map<String, dynamic>?> getPendingState(String deviceId) async {
    final url = Uri.parse(
      '${ApiConfig.baseUrl}${ApiConfig.devicesEndpoint}/$deviceId/state/pending',
    );

    try {
      _logRequest('GET', url.toString());
      final response = await http.get(url).timeout(ApiConfig.timeout);
      _logResponse('GET', url.toString(), response.statusCode, response.body);

      if (response.statusCode == 200) {
        return jsonDecode(response.body);
      } else {
        throw Exception('Failed to get pending state: ${response.statusCode}');
      }
    } catch (e) {
      _logError('GET', url.toString(), e);
      return null;
    }
  }

  /// Clear all vitals and alerts for a device (keeps registration)
  Future<Map<String, dynamic>?> clearDeviceData(String deviceId) async {
    final url = Uri.parse(
      '${ApiConfig.baseUrl}${ApiConfig.devicesEndpoint}/$deviceId/data',
    );

    try {
      _logRequest('DELETE', url.toString());
      final response = await http.delete(url).timeout(ApiConfig.timeout);
      _logResponse(
        'DELETE',
        url.toString(),
        response.statusCode,
        response.body,
      );

      if (response.statusCode == 200) {
        return jsonDecode(response.body);
      } else {
        throw Exception('Failed to clear data: ${response.statusCode}');
      }
    } catch (e) {
      _logError('DELETE', url.toString(), e);
      return null;
    }
  }
}
