import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import '../models/vitals_data.dart';

class ApiService extends ChangeNotifier {
  static const String baseUrl = 'http://your-backend-api.com/api/v1';
  String? _authToken;

  Future<bool> uploadVitals(String userId, VitalsData vitals) async {
    try {
      final response = await http.post(
        Uri.parse('$baseUrl/vitals/upload'),
        headers: {
          'Content-Type': 'application/json',
          if (_authToken != null) 'Authorization': 'Bearer $_authToken',
        },
        body: jsonEncode({
          'user_id': userId,
          ...vitals.toJson(),
        }),
      );

      return response.statusCode == 200 || response.statusCode == 201;
    } catch (e) {
      return false;
    }
  }

  Future<List<VitalsData>> getLatestVitals(String userId) async {
    try {
      final response = await http.get(
        Uri.parse('$baseUrl/vitals/latest/$userId'),
        headers: {
          if (_authToken != null) 'Authorization': 'Bearer $_authToken',
        },
      );

      if (response.statusCode == 200) {
        final List<dynamic> data = jsonDecode(response.body);
        return data.map((json) => VitalsData.fromJson(json)).toList();
      }
      return [];
    } catch (e) {
      return [];
    }
  }

  Future<Map<String, dynamic>?> getHealthPredictions(String userId) async {
    try {
      final response = await http.post(
        Uri.parse('$baseUrl/analytics/predict'),
        headers: {
          'Content-Type': 'application/json',
          if (_authToken != null) 'Authorization': 'Bearer $_authToken',
        },
        body: jsonEncode({'user_id': userId}),
      );

      if (response.statusCode == 200) {
        return jsonDecode(response.body);
      }
      return null;
    } catch (e) {
      return null;
    }
  }

  Future<Map<String, dynamic>?> getHealthRatings(String userId) async {
    try {
      final response = await http.get(
        Uri.parse('$baseUrl/ratings/$userId'),
        headers: {
          if (_authToken != null) 'Authorization': 'Bearer $_authToken',
        },
      );

      if (response.statusCode == 200) {
        return jsonDecode(response.body);
      }
      return null;
    } catch (e) {
      return null;
    }
  }

  void setAuthToken(String token) {
    _authToken = token;
    notifyListeners();
  }
}


