import 'dart:async';
import 'package:flutter/material.dart';
import '../models/charger_settings.dart';
import '../services/ble_service.dart';
import 'pairing_screen.dart';

class SettingsScreen extends StatefulWidget {
  final BleService bleService;

  const SettingsScreen({super.key, required this.bleService});

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  // Current status from device
  ChargerStatus _status = ChargerStatus.empty();
  StreamSubscription<ChargerStatus>? _statusSubscription;

  // Local settings state
  TimeOfDay? _scheduledTime;
  bool _voltageThresholdEnabled = false;
  double _voltageThreshold = 40.5;

  // UI state
  bool _isLoading = false;

  @override
  void initState() {
    super.initState();

    // Listen for status updates
    _statusSubscription = widget.bleService.statusStream.listen((status) {
      if (mounted) {
        setState(() => _status = status);
      }
    });

    // Listen for disconnection
    widget.bleService.connectionState.listen((connected) {
      if (!connected && mounted) {
        _navigateToPairingScreen();
      }
    });

    // Try to read initial status
    _readInitialStatus();
  }

  @override
  void dispose() {
    _statusSubscription?.cancel();
    super.dispose();
  }

  Future<void> _readInitialStatus() async {
    try {
      final status = await widget.bleService.readStatus();
      if (mounted) {
        setState(() => _status = status);
      }
    } catch (e) {
      // Ignore - will get status via notifications
    }
  }

  void _navigateToPairingScreen() {
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(
        content: Text('Device disconnected'),
        backgroundColor: Colors.orange,
      ),
    );
    Navigator.of(context).pushReplacement(
      MaterialPageRoute(builder: (context) => const PairingScreen()),
    );
  }

  Future<void> _uploadSettings() async {
    setState(() => _isLoading = true);

    try {
      final settings = ChargerSettings(
        scheduledStartTime: _scheduledTime != null
            ? _calculateDateTime(_scheduledTime!)
            : null,
        voltageThresholdEnabled: _voltageThresholdEnabled,
        voltageThresholdMv: (_voltageThreshold * 1000).round(),
      );

      await widget.bleService.uploadSettings(settings);

      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text('Settings saved'),
            backgroundColor: Colors.green,
          ),
        );
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('Failed to save settings: $e'),
            backgroundColor: Colors.red,
          ),
        );
      }
    } finally {
      if (mounted) {
        setState(() => _isLoading = false);
      }
    }
  }

  DateTime _calculateDateTime(TimeOfDay time) {
    final now = DateTime.now();
    var scheduled = DateTime(
      now.year,
      now.month,
      now.day,
      time.hour,
      time.minute,
    );
    // If the time has already passed today, schedule for tomorrow
    if (scheduled.isBefore(now)) {
      scheduled = scheduled.add(const Duration(days: 1));
    }
    return scheduled;
  }

  Future<void> _selectTime() async {
    final picked = await showTimePicker(
      context: context,
      initialTime: _scheduledTime ?? TimeOfDay.now(),
    );
    if (picked != null && mounted) {
      setState(() => _scheduledTime = picked);
    }
  }

  void _clearSchedule() {
    setState(() => _scheduledTime = null);
  }

  Future<void> _forceStart() async {
    try {
      await widget.bleService.forceStart();
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Failed: $e'), backgroundColor: Colors.red),
        );
      }
    }
  }

  Future<void> _forceStop() async {
    try {
      await widget.bleService.forceStop();
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Failed: $e'), backgroundColor: Colors.red),
        );
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Charger Settings'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
        actions: [
          IconButton(
            icon: const Icon(Icons.bluetooth_disabled),
            onPressed: () => widget.bleService.disconnect(),
            tooltip: 'Disconnect',
          ),
        ],
      ),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          _buildStatusCard(),
          const SizedBox(height: 16),
          _buildScheduleCard(),
          const SizedBox(height: 16),
          _buildThresholdCard(),
          const SizedBox(height: 24),
          _buildSaveButton(),
          const SizedBox(height: 16),
          _buildEmergencyControls(),
        ],
      ),
    );
  }

  Widget _buildStatusCard() {
    final isCharging = _status.relayOn;
    final stateColor = switch (_status.state) {
      ChargerState.idle => Colors.grey,
      ChargerState.scheduled => Colors.blue,
      ChargerState.charging => Colors.green,
      ChargerState.thresholdDelay => Colors.orange,
      ChargerState.complete => Colors.teal,
      ChargerState.error => Colors.red,
    };

    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Icon(
                  isCharging ? Icons.bolt : Icons.power_off,
                  color: isCharging ? Colors.amber : Colors.grey,
                  size: 32,
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        _status.state.displayName,
                        style: TextStyle(
                          fontSize: 20,
                          fontWeight: FontWeight.bold,
                          color: stateColor,
                        ),
                      ),
                      Text(
                        isCharging ? 'Relay ON' : 'Relay OFF',
                        style: TextStyle(color: Colors.grey.shade600),
                      ),
                    ],
                  ),
                ),
                Container(
                  padding: const EdgeInsets.symmetric(
                    horizontal: 12,
                    vertical: 6,
                  ),
                  decoration: BoxDecoration(
                    color: Colors.blue.shade100,
                    borderRadius: BorderRadius.circular(16),
                  ),
                  child: Text(
                    _status.voltageDisplay,
                    style: TextStyle(
                      fontWeight: FontWeight.bold,
                      color: Colors.blue.shade800,
                    ),
                  ),
                ),
              ],
            ),
            if (_status.thresholdCountdown > 0) ...[
              const SizedBox(height: 12),
              LinearProgressIndicator(
                value: _status.thresholdCountdown / 10,
                backgroundColor: Colors.orange.shade100,
                valueColor: AlwaysStoppedAnimation(Colors.orange.shade600),
              ),
              const SizedBox(height: 4),
              Text(
                'Stopping in ${_status.thresholdCountdown}s...',
                style: TextStyle(color: Colors.orange.shade700),
              ),
            ],
          ],
        ),
      ),
    );
  }

  Widget _buildScheduleCard() {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              'Scheduled Start Time',
              style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 12),
            Row(
              children: [
                Expanded(
                  child: OutlinedButton.icon(
                    onPressed: _selectTime,
                    icon: const Icon(Icons.access_time),
                    label: Text(
                      _scheduledTime != null
                          ? _scheduledTime!.format(context)
                          : 'Select Time',
                    ),
                    style: OutlinedButton.styleFrom(
                      padding: const EdgeInsets.symmetric(vertical: 16),
                    ),
                  ),
                ),
                if (_scheduledTime != null) ...[
                  const SizedBox(width: 8),
                  IconButton(
                    onPressed: _clearSchedule,
                    icon: const Icon(Icons.clear),
                    tooltip: 'Clear schedule',
                  ),
                ],
              ],
            ),
            if (_scheduledTime != null) ...[
              const SizedBox(height: 8),
              Text(
                'Charging will start at ${_scheduledTime!.format(context)} ${_calculateDateTime(_scheduledTime!).day != DateTime.now().day ? "(tomorrow)" : "(today)"}',
                style: TextStyle(color: Colors.grey.shade600, fontSize: 12),
              ),
            ],
          ],
        ),
      ),
    );
  }

  Widget _buildThresholdCard() {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                const Expanded(
                  child: Text(
                    'Voltage Threshold',
                    style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
                  ),
                ),
                Switch(
                  value: _voltageThresholdEnabled,
                  onChanged: (value) {
                    setState(() => _voltageThresholdEnabled = value);
                  },
                ),
              ],
            ),
            const SizedBox(height: 8),
            Text(
              'Auto-stop charging when voltage reaches threshold',
              style: TextStyle(color: Colors.grey.shade600, fontSize: 12),
            ),
            if (_voltageThresholdEnabled) ...[
              const SizedBox(height: 16),
              Row(
                children: [
                  Expanded(
                    child: Slider(
                      value: _voltageThreshold,
                      min: 36.0,
                      max: 42.0,
                      divisions: 60,
                      label: '${_voltageThreshold.toStringAsFixed(1)} V',
                      onChanged: (value) {
                        setState(() => _voltageThreshold = value);
                      },
                    ),
                  ),
                  SizedBox(
                    width: 60,
                    child: Text(
                      '${_voltageThreshold.toStringAsFixed(1)} V',
                      style: const TextStyle(fontWeight: FontWeight.bold),
                    ),
                  ),
                ],
              ),
            ],
          ],
        ),
      ),
    );
  }

  Widget _buildSaveButton() {
    return SizedBox(
      width: double.infinity,
      child: ElevatedButton.icon(
        onPressed: _isLoading ? null : _uploadSettings,
        icon: _isLoading
            ? const SizedBox(
                width: 20,
                height: 20,
                child: CircularProgressIndicator(strokeWidth: 2),
              )
            : const Icon(Icons.save),
        label: Text(_isLoading ? 'Uploading...' : 'Upload Settings'),
        style: ElevatedButton.styleFrom(
          padding: const EdgeInsets.symmetric(vertical: 16),
          backgroundColor: Theme.of(context).colorScheme.primary,
          foregroundColor: Theme.of(context).colorScheme.onPrimary,
        ),
      ),
    );
  }

  Widget _buildEmergencyControls() {
    return Card(
      color: Colors.grey.shade100,
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              'Manual Control',
              style: TextStyle(fontSize: 14, fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 12),
            Row(
              children: [
                Expanded(
                  child: OutlinedButton.icon(
                    onPressed: _forceStart,
                    icon: const Icon(Icons.play_arrow, color: Colors.green),
                    label: const Text('Start Now'),
                    style: OutlinedButton.styleFrom(
                      foregroundColor: Colors.green,
                    ),
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: OutlinedButton.icon(
                    onPressed: _forceStop,
                    icon: const Icon(Icons.stop, color: Colors.red),
                    label: const Text('Stop'),
                    style: OutlinedButton.styleFrom(
                      foregroundColor: Colors.red,
                    ),
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
