import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../services/ble_service.dart';
import 'control_screen.dart';

class PairingScreen extends StatefulWidget {
  const PairingScreen({super.key});

  @override
  State<PairingScreen> createState() => _PairingScreenState();
}

class _PairingScreenState extends State<PairingScreen> {
  final BleService _bleService = BleService();
  List<ScanResult> _scanResults = [];
  bool _isScanning = false;
  String? _error;

  @override
  void initState() {
    super.initState();
    _bleService.scanResults.listen((results) {
      if (mounted) {
        setState(() => _scanResults = results);
      }
    });
  }

  @override
  void dispose() {
    _bleService.stopScan();
    super.dispose();
  }

  Future<void> _startScan() async {
    setState(() {
      _isScanning = true;
      _error = null;
      _scanResults = [];
    });

    try {
      await _bleService.startScan();
      // Wait for scan to complete
      await Future.delayed(const Duration(seconds: 10));
    } catch (e) {
      if (mounted) {
        setState(() => _error = e.toString());
      }
    } finally {
      if (mounted) {
        setState(() => _isScanning = false);
      }
    }
  }

  Future<void> _connectToDevice(BluetoothDevice device) async {
    try {
      await _bleService.stopScan();

      if (!mounted) return;

      // Show connecting dialog
      showDialog(
        context: context,
        barrierDismissible: false,
        builder: (context) => const AlertDialog(
          content: Row(
            children: [
              CircularProgressIndicator(),
              SizedBox(width: 20),
              Text('Connecting...'),
            ],
          ),
        ),
      );

      await _bleService.connect(device);

      if (!mounted) return;
      Navigator.of(context).pop(); // Close dialog

      // Navigate to control screen
      Navigator.of(context).pushReplacement(
        MaterialPageRoute(
          builder: (context) => ControlScreen(bleService: _bleService),
        ),
      );
    } catch (e) {
      if (!mounted) return;
      Navigator.of(context).pop(); // Close dialog
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text('Connection failed: $e'),
          backgroundColor: Colors.red,
        ),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Find ESP Device'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      body: Column(
        children: [
          // Scan button
          Padding(
            padding: const EdgeInsets.all(16.0),
            child: SizedBox(
              width: double.infinity,
              child: ElevatedButton.icon(
                onPressed: _isScanning ? null : _startScan,
                icon: _isScanning
                    ? const SizedBox(
                        width: 20,
                        height: 20,
                        child: CircularProgressIndicator(strokeWidth: 2),
                      )
                    : const Icon(Icons.bluetooth_searching),
                label: Text(_isScanning ? 'Scanning...' : 'Scan for Devices'),
                style: ElevatedButton.styleFrom(
                  padding: const EdgeInsets.symmetric(vertical: 16),
                ),
              ),
            ),
          ),

          // Error message
          if (_error != null)
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 16.0),
              child: Card(
                color: Colors.red.shade100,
                child: Padding(
                  padding: const EdgeInsets.all(12.0),
                  child: Row(
                    children: [
                      const Icon(Icons.error, color: Colors.red),
                      const SizedBox(width: 8),
                      Expanded(
                        child: Text(
                          _error!,
                          style: const TextStyle(color: Colors.red),
                        ),
                      ),
                    ],
                  ),
                ),
              ),
            ),

          // Device list
          Expanded(
            child: _scanResults.isEmpty
                ? Center(
                    child: Column(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        Icon(
                          Icons.bluetooth_disabled,
                          size: 64,
                          color: Colors.grey.shade400,
                        ),
                        const SizedBox(height: 16),
                        Text(
                          _isScanning
                              ? 'Searching for devices...'
                              : 'No devices found.\nTap "Scan" to search.',
                          textAlign: TextAlign.center,
                          style: TextStyle(
                            color: Colors.grey.shade600,
                            fontSize: 16,
                          ),
                        ),
                        const SizedBox(height: 8),
                        Text(
                          'Make sure your ESP32 is powered on.',
                          textAlign: TextAlign.center,
                          style: TextStyle(
                            color: Colors.grey.shade500,
                            fontSize: 14,
                          ),
                        ),
                      ],
                    ),
                  )
                : ListView.builder(
                    itemCount: _scanResults.length,
                    itemBuilder: (context, index) {
                      final result = _scanResults[index];
                      final deviceName = result.device.platformName.isEmpty
                          ? 'Unknown Device'
                          : result.device.platformName;

                      return Card(
                        margin: const EdgeInsets.symmetric(
                          horizontal: 16,
                          vertical: 4,
                        ),
                        child: ListTile(
                          leading: const CircleAvatar(
                            child: Icon(Icons.bluetooth),
                          ),
                          title: Text(
                            deviceName,
                            style: const TextStyle(fontWeight: FontWeight.bold),
                          ),
                          subtitle: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              Text(
                                result.device.remoteId.toString(),
                                style: TextStyle(
                                  fontSize: 12,
                                  color: Colors.grey.shade600,
                                ),
                              ),
                              const SizedBox(height: 2),
                              Text(
                                'Signal: ${result.rssi} dBm',
                                style: TextStyle(
                                  fontSize: 12,
                                  color: _getSignalColor(result.rssi),
                                ),
                              ),
                            ],
                          ),
                          trailing: const Icon(Icons.chevron_right),
                          onTap: () => _connectToDevice(result.device),
                        ),
                      );
                    },
                  ),
          ),
        ],
      ),
    );
  }

  Color _getSignalColor(int rssi) {
    if (rssi >= -60) return Colors.green;
    if (rssi >= -80) return Colors.orange;
    return Colors.red;
  }
}
