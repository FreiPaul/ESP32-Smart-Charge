import 'dart:async';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

/// BLE Service for communicating with ESP32-C6 LED Controller
class BleService {
  // UUIDs matching ESP32 firmware
  static const String ledServiceUuid = "12345678-1234-5678-1234-56789abcdef0";
  static const String ledCharUuid = "12345678-1234-5678-1234-56789abcdef1";

  BluetoothDevice? _connectedDevice;
  BluetoothCharacteristic? _ledCharacteristic;
  StreamSubscription<BluetoothConnectionState>? _connectionSubscription;

  final _connectionStateController = StreamController<bool>.broadcast();
  final _scanResultsController = StreamController<List<ScanResult>>.broadcast();

  /// Stream of connection state changes
  Stream<bool> get connectionState => _connectionStateController.stream;

  /// Stream of scan results (filtered for ESP-LED devices)
  Stream<List<ScanResult>> get scanResults => _scanResultsController.stream;

  /// Whether currently connected to a device
  bool get isConnected => _connectedDevice != null;

  /// Name of connected device
  String? get connectedDeviceName => _connectedDevice?.platformName;

  /// Start scanning for ESP-LED devices
  Future<void> startScan({Duration timeout = const Duration(seconds: 10)}) async {
    // Check if Bluetooth is on
    final adapterState = await FlutterBluePlus.adapterState.first;
    if (adapterState != BluetoothAdapterState.on) {
      throw Exception('Bluetooth is not enabled. Please enable Bluetooth in Settings.');
    }

    // Stop any existing scan
    await FlutterBluePlus.stopScan();

    // Listen to scan results
    FlutterBluePlus.onScanResults.listen((results) {
      // Filter for devices that either:
      // 1. Have "ESP-LED" in their name
      // 2. Advertise our LED service UUID
      final filtered = results.where((r) {
        final hasMatchingName = r.device.platformName.contains('ESP-LED');
        final hasMatchingService = r.advertisementData.serviceUuids.any(
          (uuid) => uuid.toString().toLowerCase() == ledServiceUuid.toLowerCase(),
        );
        return hasMatchingName || hasMatchingService;
      }).toList();

      _scanResultsController.add(filtered);
    });

    // Start scanning with service filter
    await FlutterBluePlus.startScan(
      withServices: [Guid(ledServiceUuid)],
      timeout: timeout,
    );
  }

  /// Stop scanning
  Future<void> stopScan() async {
    await FlutterBluePlus.stopScan();
  }

  /// Connect to a BLE device
  Future<void> connect(BluetoothDevice device) async {
    try {
      // Stop scanning before connecting (better compatibility)
      await FlutterBluePlus.stopScan();

      // Connect with timeout
      await device.connect(timeout: const Duration(seconds: 15));
      _connectedDevice = device;

      // Listen for disconnection
      _connectionSubscription = device.connectionState.listen((state) {
        if (state == BluetoothConnectionState.disconnected) {
          _handleDisconnect();
        }
      });

      // Discover services
      final services = await device.discoverServices();

      // Find our LED service and characteristic
      for (var service in services) {
        if (service.uuid.toString().toLowerCase() == ledServiceUuid.toLowerCase()) {
          for (var char in service.characteristics) {
            if (char.uuid.toString().toLowerCase() == ledCharUuid.toLowerCase()) {
              _ledCharacteristic = char;
              break;
            }
          }
        }
      }

      if (_ledCharacteristic == null) {
        await device.disconnect();
        throw Exception('LED characteristic not found on device');
      }

      _connectionStateController.add(true);
    } catch (e) {
      _handleDisconnect();
      rethrow;
    }
  }

  /// Disconnect from current device
  Future<void> disconnect() async {
    await _connectedDevice?.disconnect();
    _handleDisconnect();
  }

  void _handleDisconnect() {
    _connectionSubscription?.cancel();
    _connectionSubscription = null;
    _connectedDevice = null;
    _ledCharacteristic = null;
    _connectionStateController.add(false);
  }

  /// Read current LED state
  /// Returns true if LED is ON, false if OFF
  Future<bool> readLedState() async {
    if (_ledCharacteristic == null) {
      throw Exception('Not connected to device');
    }

    final value = await _ledCharacteristic!.read();
    return value.isNotEmpty && value[0] != 0;
  }

  /// Write LED state
  /// [on] - true to turn LED on, false to turn off
  Future<void> writeLedState(bool on) async {
    if (_ledCharacteristic == null) {
      throw Exception('Not connected to device');
    }

    await _ledCharacteristic!.write([on ? 0x01 : 0x00]);
  }

  /// Toggle LED and return new state
  Future<bool> toggleLed() async {
    final currentState = await readLedState();
    final newState = !currentState;
    await writeLedState(newState);
    return newState;
  }

  /// Dispose resources
  void dispose() {
    _connectionSubscription?.cancel();
    _connectionStateController.close();
    _scanResultsController.close();
  }
}
