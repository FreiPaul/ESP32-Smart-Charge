import 'package:flutter/material.dart';
import '../services/ble_service.dart';
import 'pairing_screen.dart';

class ControlScreen extends StatefulWidget {
  final BleService bleService;

  const ControlScreen({super.key, required this.bleService});

  @override
  State<ControlScreen> createState() => _ControlScreenState();
}

class _ControlScreenState extends State<ControlScreen> {
  bool _ledState = false;
  bool _isLoading = true;
  bool _isToggling = false;

  @override
  void initState() {
    super.initState();
    _readInitialState();

    // Listen for disconnection
    widget.bleService.connectionState.listen((connected) {
      if (!connected && mounted) {
        _navigateToParingScreen();
      }
    });
  }

  void _navigateToParingScreen() {
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

  Future<void> _readInitialState() async {
    try {
      final state = await widget.bleService.readLedState();
      if (mounted) {
        setState(() {
          _ledState = state;
          _isLoading = false;
        });
      }
    } catch (e) {
      if (mounted) {
        setState(() => _isLoading = false);
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('Failed to read LED state: $e'),
            backgroundColor: Colors.red,
          ),
        );
      }
    }
  }

  Future<void> _toggleLed() async {
    if (_isToggling) return;

    setState(() => _isToggling = true);

    try {
      final newState = await widget.bleService.toggleLed();
      if (mounted) {
        setState(() => _ledState = newState);
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('Failed to toggle LED: $e'),
            backgroundColor: Colors.red,
          ),
        );
      }
    } finally {
      if (mounted) {
        setState(() => _isToggling = false);
      }
    }
  }

  Future<void> _disconnect() async {
    await widget.bleService.disconnect();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('LED Control'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
        actions: [
          IconButton(
            icon: const Icon(Icons.bluetooth_disabled),
            onPressed: _disconnect,
            tooltip: 'Disconnect',
          ),
        ],
      ),
      body: Center(
        child: _isLoading
            ? const Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  CircularProgressIndicator(),
                  SizedBox(height: 16),
                  Text('Reading LED state...'),
                ],
              )
            : Padding(
                padding: const EdgeInsets.all(24.0),
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    // Connection indicator
                    Container(
                      padding: const EdgeInsets.symmetric(
                        horizontal: 16,
                        vertical: 8,
                      ),
                      decoration: BoxDecoration(
                        color: Colors.green.shade100,
                        borderRadius: BorderRadius.circular(20),
                      ),
                      child: Row(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          Icon(
                            Icons.bluetooth_connected,
                            color: Colors.green.shade700,
                            size: 20,
                          ),
                          const SizedBox(width: 8),
                          Text(
                            'Connected to ${widget.bleService.connectedDeviceName ?? "ESP-LED-CTRL"}',
                            style: TextStyle(
                              color: Colors.green.shade700,
                              fontWeight: FontWeight.w500,
                            ),
                          ),
                        ],
                      ),
                    ),

                    const Spacer(),

                    // LED icon
                    AnimatedContainer(
                      duration: const Duration(milliseconds: 300),
                      child: Icon(
                        Icons.lightbulb,
                        size: 160,
                        color: _ledState
                            ? Colors.yellow.shade600
                            : Colors.grey.shade400,
                        shadows: _ledState
                            ? [
                                Shadow(
                                  color: Colors.yellow.withAlpha(128),
                                  blurRadius: 40,
                                ),
                              ]
                            : null,
                      ),
                    ),

                    const SizedBox(height: 24),

                    // Status text
                    Text(
                      _ledState ? 'LED is ON' : 'LED is OFF',
                      style: Theme.of(context).textTheme.headlineMedium?.copyWith(
                            fontWeight: FontWeight.bold,
                            color: _ledState
                                ? Colors.yellow.shade800
                                : Colors.grey.shade600,
                          ),
                    ),

                    const Spacer(),

                    // Toggle button
                    SizedBox(
                      width: double.infinity,
                      child: ElevatedButton(
                        onPressed: _isToggling ? null : _toggleLed,
                        style: ElevatedButton.styleFrom(
                          padding: const EdgeInsets.symmetric(vertical: 20),
                          backgroundColor: _ledState
                              ? Colors.grey.shade300
                              : Colors.yellow.shade600,
                          foregroundColor:
                              _ledState ? Colors.black87 : Colors.black,
                          shape: RoundedRectangleBorder(
                            borderRadius: BorderRadius.circular(16),
                          ),
                        ),
                        child: _isToggling
                            ? const SizedBox(
                                width: 24,
                                height: 24,
                                child: CircularProgressIndicator(strokeWidth: 2),
                              )
                            : Text(
                                _ledState ? 'TURN OFF' : 'TURN ON',
                                style: const TextStyle(
                                  fontSize: 20,
                                  fontWeight: FontWeight.bold,
                                ),
                              ),
                      ),
                    ),

                    const SizedBox(height: 16),

                    // Disconnect button
                    TextButton.icon(
                      onPressed: _disconnect,
                      icon: const Icon(Icons.bluetooth_disabled),
                      label: const Text('Disconnect'),
                      style: TextButton.styleFrom(
                        foregroundColor: Colors.red,
                      ),
                    ),

                    const SizedBox(height: 32),
                  ],
                ),
              ),
      ),
    );
  }
}
