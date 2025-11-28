import 'dart:async';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import '../models/charger_settings.dart';

/// BLE Service for communicating with ESP32-C6 Smart Charger
class BleService {
  // UUIDs matching ESP32 firmware
  static const String serviceUuid = "12345678-1234-5678-1234-56789abcdef0";
  static const String settingsCharUuid = "12345678-1234-5678-1234-56789abcdef2";
  static const String statusCharUuid = "12345678-1234-5678-1234-56789abcdef3";
  static const String commandCharUuid = "12345678-1234-5678-1234-56789abcdef4";

  BluetoothDevice? _connectedDevice;
  BluetoothCharacteristic? _settingsCharacteristic;
  BluetoothCharacteristic? _statusCharacteristic;
  BluetoothCharacteristic? _commandCharacteristic;
  StreamSubscription<BluetoothConnectionState>? _connectionSubscription;
  StreamSubscription<List<int>>? _statusSubscription;

  final _connectionStateController = StreamController<bool>.broadcast();
  final _scanResultsController = StreamController<List<ScanResult>>.broadcast();
  final _statusController = StreamController<ChargerStatus>.broadcast();

  /// Stream of connection state changes
  Stream<bool> get connectionState => _connectionStateController.stream;

  /// Stream of scan results (filtered for ESP-CHARGER devices)
  Stream<List<ScanResult>> get scanResults => _scanResultsController.stream;

  /// Stream of status updates from device
  Stream<ChargerStatus> get statusStream => _statusController.stream;

  /// Whether currently connected to a device
  bool get isConnected => _connectedDevice != null;

  /// Name of connected device
  String? get connectedDeviceName => _connectedDevice?.platformName;

  /// Start scanning for ESP-CHARGER devices
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
      // 1. Have "ESP-CHARGER" or "ESP-LED" in their name
      // 2. Advertise our service UUID
      final filtered = results.where((r) {
        final name = r.device.platformName;
        final hasMatchingName = name.contains('ESP-CHARGER') || name.contains('ESP-LED');
        final hasMatchingService = r.advertisementData.serviceUuids.any(
          (uuid) => uuid.toString().toLowerCase() == serviceUuid.toLowerCase(),
        );
        return hasMatchingName || hasMatchingService;
      }).toList();

      _scanResultsController.add(filtered);
    });

    // Start scanning with service filter
    await FlutterBluePlus.startScan(
      withServices: [Guid(serviceUuid)],
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

      // Find our service and characteristics
      for (var service in services) {
        if (service.uuid.toString().toLowerCase() == serviceUuid.toLowerCase()) {
          for (var char in service.characteristics) {
            final charUuid = char.uuid.toString().toLowerCase();
            if (charUuid == settingsCharUuid.toLowerCase()) {
              _settingsCharacteristic = char;
            } else if (charUuid == statusCharUuid.toLowerCase()) {
              _statusCharacteristic = char;
            } else if (charUuid == commandCharUuid.toLowerCase()) {
              _commandCharacteristic = char;
            }
          }
        }
      }

      // Verify we found at least the settings characteristic
      if (_settingsCharacteristic == null) {
        await device.disconnect();
        throw Exception('Settings characteristic not found on device');
      }

      // Subscribe to status notifications if available
      if (_statusCharacteristic != null) {
        await _subscribeToStatus();
      }

      _connectionStateController.add(true);
    } catch (e) {
      _handleDisconnect();
      rethrow;
    }
  }

  /// Subscribe to status notifications
  Future<void> _subscribeToStatus() async {
    if (_statusCharacteristic == null) return;

    try {
      await _statusCharacteristic!.setNotifyValue(true);
      _statusSubscription = _statusCharacteristic!.onValueReceived.listen((data) {
        try {
          final status = ChargerStatus.fromBytes(data);
          _statusController.add(status);
        } catch (e) {
          // Ignore parse errors
        }
      });
    } catch (e) {
      // Notifications might not be supported
    }
  }

  /// Disconnect from current device
  Future<void> disconnect() async {
    await _connectedDevice?.disconnect();
    _handleDisconnect();
  }

  void _handleDisconnect() {
    _statusSubscription?.cancel();
    _statusSubscription = null;
    _connectionSubscription?.cancel();
    _connectionSubscription = null;
    _connectedDevice = null;
    _settingsCharacteristic = null;
    _statusCharacteristic = null;
    _commandCharacteristic = null;
    _connectionStateController.add(false);
  }

  /// Upload settings to the ESP
  Future<void> uploadSettings(ChargerSettings settings) async {
    if (_settingsCharacteristic == null) {
      throw Exception('Not connected to device');
    }

    await _settingsCharacteristic!.write(settings.toBytes().toList());
  }

  /// Read current status from the ESP
  Future<ChargerStatus> readStatus() async {
    if (_statusCharacteristic == null) {
      throw Exception('Status characteristic not available');
    }

    final data = await _statusCharacteristic!.read();
    return ChargerStatus.fromBytes(data);
  }

  /// Send a command to the ESP
  Future<void> sendCommand(ChargerCommand command) async {
    if (_commandCharacteristic == null) {
      throw Exception('Command characteristic not available');
    }

    await _commandCharacteristic!.write(command.toBytes());
  }

  /// Force start charging
  Future<void> forceStart() => sendCommand(ChargerCommand.forceStart);

  /// Force stop charging
  Future<void> forceStop() => sendCommand(ChargerCommand.forceStop);

  /// Clear any scheduled charging
  Future<void> clearSchedule() => sendCommand(ChargerCommand.clearSchedule);

  /// Dispose resources
  void dispose() {
    _statusSubscription?.cancel();
    _connectionSubscription?.cancel();
    _connectionStateController.close();
    _scanResultsController.close();
    _statusController.close();
  }
}
