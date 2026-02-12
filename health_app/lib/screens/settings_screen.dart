import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../services/health_api_service.dart';

class SettingsScreen extends StatefulWidget {
  final String deviceId;

  const SettingsScreen({super.key, required this.deviceId});

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  final HealthApiService _apiService = HealthApiService();
  late SharedPreferences _prefs;

  // Settings
  bool _notificationsEnabled = true;
  bool _criticalAlertsOnly = false;
  double _restingHR = 70.0;
  int _refreshInterval = 5;
  bool _darkMode = false;

  bool _isLoading = true;
  bool _isSaving = false;

  @override
  void initState() {
    super.initState();
    _loadSettings();
  }

  Future<void> _loadSettings() async {
    _prefs = await SharedPreferences.getInstance();

    setState(() {
      _notificationsEnabled = _prefs.getBool('notifications_enabled') ?? true;
      _criticalAlertsOnly = _prefs.getBool('critical_alerts_only') ?? false;
      _restingHR = _prefs.getDouble('resting_hr') ?? 70.0;
      _refreshInterval = _prefs.getInt('refresh_interval') ?? 5;
      _darkMode = _prefs.getBool('dark_mode') ?? false;
      _isLoading = false;
    });
  }

  Future<void> _saveSetting(String key, dynamic value) async {
    if (value is bool) {
      await _prefs.setBool(key, value);
    } else if (value is int) {
      await _prefs.setInt(key, value);
    } else if (value is double) {
      await _prefs.setDouble(key, value);
    }
  }

  Future<void> _calibrateRestingHR() async {
    setState(() => _isSaving = true);

    final success = await _apiService.setRestingHR(widget.deviceId, _restingHR);

    if (mounted) {
      setState(() => _isSaving = false);

      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(
            success
                ? 'Resting HR updated successfully'
                : 'Failed to update resting HR',
          ),
          backgroundColor: success ? Colors.green : Colors.red,
        ),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    if (_isLoading) {
      return const Scaffold(body: Center(child: CircularProgressIndicator()));
    }

    return Scaffold(
      appBar: AppBar(title: const Text('Settings')),
      body: ListView(
        children: [
          // Notifications Section
          _buildSectionHeader('Notifications'),
          SwitchListTile(
            title: const Text('Enable Notifications'),
            subtitle: const Text('Receive alerts for health warnings'),
            value: _notificationsEnabled,
            onChanged: (value) {
              setState(() => _notificationsEnabled = value);
              _saveSetting('notifications_enabled', value);
            },
          ),
          Opacity(
            opacity: _notificationsEnabled ? 1.0 : 0.5,
            child: SwitchListTile(
              title: const Text('Critical Alerts Only'),
              subtitle: const Text(
                'Only notify for critical conditions (SpO2 < 90%)',
              ),
              value: _criticalAlertsOnly,
              onChanged: _notificationsEnabled
                  ? (value) {
                      setState(() => _criticalAlertsOnly = value);
                      _saveSetting('critical_alerts_only', value);
                    }
                  : null,
            ),
          ),
          const Divider(),

          // Calibration Section
          _buildSectionHeader('Calibration'),
          ListTile(
            title: const Text('Resting Heart Rate'),
            subtitle: Text(
              '${_restingHR.toInt()} BPM - Used for temperature estimation',
            ),
            trailing: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                IconButton(
                  icon: const Icon(Icons.remove_circle_outline),
                  onPressed: () {
                    if (_restingHR > 40) {
                      setState(() => _restingHR -= 1);
                      _saveSetting('resting_hr', _restingHR);
                    }
                  },
                ),
                Text(
                  _restingHR.toInt().toString(),
                  style: const TextStyle(
                    fontSize: 18,
                    fontWeight: FontWeight.bold,
                  ),
                ),
                IconButton(
                  icon: const Icon(Icons.add_circle_outline),
                  onPressed: () {
                    if (_restingHR < 100) {
                      setState(() => _restingHR += 1);
                      _saveSetting('resting_hr', _restingHR);
                    }
                  },
                ),
              ],
            ),
          ),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
            child: FilledButton.icon(
              onPressed: _isSaving ? null : _calibrateRestingHR,
              icon: _isSaving
                  ? const SizedBox(
                      width: 16,
                      height: 16,
                      child: CircularProgressIndicator(
                        strokeWidth: 2,
                        color: Colors.white,
                      ),
                    )
                  : const Icon(Icons.sync),
              label: Text(_isSaving ? 'Syncing...' : 'Sync to Device'),
            ),
          ),
          const Padding(
            padding: EdgeInsets.all(16),
            child: Text(
              'Tip: Sit still for 5 minutes, then note your average heart rate and set it here for accurate temperature estimation.',
              style: TextStyle(fontSize: 12, fontStyle: FontStyle.italic),
            ),
          ),
          const Divider(),

          // App Preferences Section
          _buildSectionHeader('App Preferences'),
          ListTile(
            title: const Text('Refresh Interval'),
            subtitle: const Text('How often to fetch new data'),
            trailing: DropdownButton<int>(
              value: _refreshInterval,
              items: const [
                DropdownMenuItem(value: 3, child: Text('3 seconds')),
                DropdownMenuItem(value: 5, child: Text('5 seconds')),
                DropdownMenuItem(value: 10, child: Text('10 seconds')),
                DropdownMenuItem(value: 30, child: Text('30 seconds')),
              ],
              onChanged: (value) {
                if (value != null) {
                  setState(() => _refreshInterval = value);
                  _saveSetting('refresh_interval', value);
                }
              },
            ),
          ),
          SwitchListTile(
            title: const Text('Dark Mode'),
            subtitle: const Text('Use dark theme'),
            value: _darkMode,
            onChanged: (value) {
              setState(() => _darkMode = value);
              _saveSetting('dark_mode', value);
              // TODO: Implement theme switching
            },
          ),
          const Divider(),

          // Device Info Section
          _buildSectionHeader('Device Information'),
          ListTile(
            title: const Text('Device ID'),
            subtitle: Text(widget.deviceId),
            trailing: IconButton(
              icon: const Icon(Icons.copy),
              onPressed: () {
                // TODO: Copy to clipboard
                ScaffoldMessenger.of(context).showSnackBar(
                  const SnackBar(content: Text('Device ID copied')),
                );
              },
            ),
          ),
          ListTile(
            title: const Text('Backend Server'),
            subtitle: const Text(
              'https://xenophobic-netta-cybergenii-1584fde7.koyeb.app',
            ),
            trailing: const Icon(Icons.cloud),
          ),
          const Divider(),

          // About Section
          _buildSectionHeader('About'),
          const ListTile(title: Text('Version'), subtitle: Text('1.0.0')),
          ListTile(
            title: const Text('Help & Support'),
            trailing: const Icon(Icons.arrow_forward_ios, size: 16),
            onTap: () {
              // TODO: Navigate to help screen
            },
          ),
          ListTile(
            title: const Text('Privacy Policy'),
            trailing: const Icon(Icons.arrow_forward_ios, size: 16),
            onTap: () {
              // TODO: Show privacy policy
            },
          ),
          const SizedBox(height: 16),

          // Danger Zone
          Padding(
            padding: const EdgeInsets.all(16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Text(
                  'DATA MANAGEMENT',
                  style: TextStyle(
                    fontSize: 12,
                    fontWeight: FontWeight.bold,
                    color: Colors.red[700],
                  ),
                ),
                const SizedBox(height: 8),
                OutlinedButton.icon(
                  onPressed: () {
                    showDialog(
                      context: context,
                      builder: (context) => AlertDialog(
                        title: const Text('Reset Data'),
                        content: const Text('Choose what data to clear:'),
                        actions: [
                          TextButton(
                            onPressed: () => Navigator.pop(context),
                            child: const Text('Cancel'),
                          ),
                          TextButton(
                            onPressed: () async {
                              // Clear only local app data
                              await _prefs.clear();
                              if (context.mounted) {
                                Navigator.pop(context);
                                ScaffoldMessenger.of(context).showSnackBar(
                                  const SnackBar(
                                    content: Text('App data cleared'),
                                    backgroundColor: Colors.green,
                                  ),
                                );
                              }
                            },
                            child: const Text(
                              'App Only',
                              style: TextStyle(color: Colors.orange),
                            ),
                          ),
                          TextButton(
                            onPressed: () async {
                              // Clear both app AND backend data
                              Navigator.pop(context);

                              // Show loading
                              showDialog(
                                context: context,
                                barrierDismissible: false,
                                builder: (context) => const Center(
                                  child: CircularProgressIndicator(),
                                ),
                              );

                              final result = await _apiService.clearDeviceData(
                                widget.deviceId,
                              );

                              if (context.mounted) {
                                Navigator.pop(context); // Close loading

                                if (result != null) {
                                  await _prefs.clear();
                                  ScaffoldMessenger.of(context).showSnackBar(
                                    SnackBar(
                                      content: Text(
                                        'All data cleared: ${result['message']}',
                                      ),
                                      backgroundColor: Colors.green,
                                    ),
                                  );
                                } else {
                                  ScaffoldMessenger.of(context).showSnackBar(
                                    const SnackBar(
                                      content: Text(
                                        'Failed to clear backend data',
                                      ),
                                      backgroundColor: Colors.red,
                                    ),
                                  );
                                }
                              }
                            },
                            child: const Text(
                              'Full Reset',
                              style: TextStyle(color: Colors.red),
                            ),
                          ),
                        ],
                      ),
                    );
                  },
                  icon: const Icon(Icons.delete_sweep, color: Colors.red),
                  label: const Text(
                    'Reset All Data',
                    style: TextStyle(color: Colors.red),
                  ),
                  style: OutlinedButton.styleFrom(
                    side: const BorderSide(color: Colors.red),
                  ),
                ),
                const SizedBox(height: 4),
                const Text(
                  '• App Only: Clears local settings\n• Full Reset: Clears vitals & alerts from server',
                  style: TextStyle(fontSize: 11, fontStyle: FontStyle.italic),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildSectionHeader(String title) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 16, 16, 8),
      child: Text(
        title,
        style: TextStyle(
          fontSize: 14,
          fontWeight: FontWeight.bold,
          color: Theme.of(context).colorScheme.primary,
        ),
      ),
    );
  }
}
