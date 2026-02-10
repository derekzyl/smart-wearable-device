/// API Configuration
/// Backend URL for health monitoring system
class ApiConfig {
  static const String baseUrl =
      'https://xenophobic-netta-cybergenii-1584fde7.koyeb.app';

  // Health endpoints
  static const String healthBase = '/health';
  static const String vitalsEndpoint = '$healthBase/vitals';
  static const String devicesEndpoint = '$healthBase/devices';
  static const String alertsEndpoint = '$healthBase/alerts';
  static const String analyticsEndpoint = '$healthBase/analytics';
  static const String thresholdsEndpoint = '$healthBase/thresholds';

  // Connection timeout
  static const Duration timeout = Duration(seconds: 10);

  // Refresh intervals
  static const Duration vitalsRefreshInterval = Duration(seconds: 5);
  static const Duration alertsRefreshInterval = Duration(seconds: 10);
}
