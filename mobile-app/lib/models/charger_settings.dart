import 'dart:typed_data';

/// Protocol version
const int settingsVersion = 0x01;

/// FSM States
enum ChargerState {
  idle(0, 'Idle'),
  scheduled(1, 'Scheduled'),
  charging(2, 'Charging'),
  thresholdDelay(3, 'Stopping...'),
  complete(4, 'Complete'),
  error(5, 'Error');

  final int value;
  final String displayName;

  const ChargerState(this.value, this.displayName);

  static ChargerState fromValue(int value) {
    return ChargerState.values.firstWhere(
      (s) => s.value == value,
      orElse: () => ChargerState.idle,
    );
  }
}

/// Settings to be uploaded to ESP
class ChargerSettings {
  DateTime? scheduledStartTime;
  bool voltageThresholdEnabled;
  int voltageThresholdMv;

  ChargerSettings({
    this.scheduledStartTime,
    this.voltageThresholdEnabled = false,
    this.voltageThresholdMv = 14400, // Default 14.4V
  });

  /// Voltage threshold in volts
  double get voltageThresholdV => voltageThresholdMv / 1000.0;
  set voltageThresholdV(double v) => voltageThresholdMv = (v * 1000).round();

  /// Whether a schedule is set
  bool get hasSchedule => scheduledStartTime != null;

  /// Serialize to BLE packet (14 bytes)
  Uint8List toBytes() {
    final buffer = ByteData(14);
    int flags = 0;

    // Byte 0: Version
    buffer.setUint8(0, settingsVersion);

    // Byte 1-4: Current Unix timestamp (little-endian)
    final now = DateTime.now().millisecondsSinceEpoch ~/ 1000;
    buffer.setUint32(1, now, Endian.little);

    // Byte 5: Flags
    if (voltageThresholdEnabled) flags |= 0x01;
    if (scheduledStartTime != null) flags |= 0x02;
    buffer.setUint8(5, flags);

    // Byte 6-9: Scheduled start time (little-endian)
    final startTime = scheduledStartTime?.millisecondsSinceEpoch ?? 0;
    buffer.setUint32(6, startTime ~/ 1000, Endian.little);

    // Byte 10-11: Voltage threshold (little-endian)
    buffer.setUint16(10, voltageThresholdMv, Endian.little);

    // Byte 12: Reserved
    buffer.setUint8(12, 0);

    // Byte 13: Checksum (XOR of bytes 0-12)
    int checksum = 0;
    for (int i = 0; i < 13; i++) {
      checksum ^= buffer.getUint8(i);
    }
    buffer.setUint8(13, checksum);

    return buffer.buffer.asUint8List();
  }

  /// Create settings for immediate start (no schedule)
  factory ChargerSettings.immediateStart({
    bool voltageThresholdEnabled = false,
    int voltageThresholdMv = 14400,
  }) {
    return ChargerSettings(
      scheduledStartTime: DateTime.now().subtract(const Duration(seconds: 1)),
      voltageThresholdEnabled: voltageThresholdEnabled,
      voltageThresholdMv: voltageThresholdMv,
    );
  }
}

/// Status received from ESP
class ChargerStatus {
  final ChargerState state;
  final int voltageMv;
  final DateTime deviceTime;
  final bool relayOn;
  final int thresholdCountdown;
  final int errorCode;
  final DateTime scheduledStartTime;
  final int voltageThresholdMv;
  final int flags;

  ChargerStatus({
    required this.state,
    required this.voltageMv,
    required this.deviceTime,
    required this.relayOn,
    required this.thresholdCountdown,
    required this.errorCode,
    required this.scheduledStartTime,
    required this.voltageThresholdMv,
    required this.flags,
  });

  /// Voltage in volts
  double get voltageV => voltageMv / 1000.0;

  /// Voltage threshold in volts
  double get voltageThresholdV => voltageThresholdMv / 1000.0;

  /// Whether voltage threshold is enabled (flag bit 0)
  bool get voltageThresholdEnabled => (flags & 0x01) != 0;

  /// Whether schedule is enabled (flag bit 1)
  bool get scheduleEnabled => (flags & 0x02) != 0;

  /// Whether time has been synchronized
  bool get timeIsSynced => deviceTime.millisecondsSinceEpoch > 0;

  /// Format voltage for display
  String get voltageDisplay => '${voltageV.toStringAsFixed(2)} V';

  /// Parse from BLE packet (18 bytes)
  factory ChargerStatus.fromBytes(List<int> data) {
    if (data.length < 18) {
      throw ArgumentError(
        'Invalid status data: expected 18 bytes, got ${data.length}',
      );
    }

    final buffer = ByteData.view(Uint8List.fromList(data).buffer);

    // Verify version
    final version = buffer.getUint8(0);
    if (version != settingsVersion) {
      throw ArgumentError(
        'Invalid status version: expected $settingsVersion, got $version',
      );
    }

    return ChargerStatus(
      state: ChargerState.fromValue(buffer.getUint8(1)),
      voltageMv: buffer.getUint16(2, Endian.little),
      deviceTime: DateTime.fromMillisecondsSinceEpoch(
        buffer.getUint32(4, Endian.little) * 1000,
      ),
      relayOn: buffer.getUint8(8) != 0,
      thresholdCountdown: buffer.getUint8(9),
      errorCode: buffer.getUint8(10),
      scheduledStartTime: DateTime.fromMillisecondsSinceEpoch(
        buffer.getUint32(11, Endian.little) * 1000,
      ),
      voltageThresholdMv: buffer.getUint16(15, Endian.little),
      flags: buffer.getUint8(17),
    );
  }

  /// Create a default/empty status
  factory ChargerStatus.empty() {
    return ChargerStatus(
      state: ChargerState.idle,
      voltageMv: 0,
      deviceTime: DateTime.fromMillisecondsSinceEpoch(0),
      relayOn: false,
      thresholdCountdown: 0,
      errorCode: 0,
      scheduledStartTime: DateTime.fromMillisecondsSinceEpoch(0),
      voltageThresholdMv: 40500, // Default 40.5V
      flags: 0,
    );
  }
}

/// Command codes
enum ChargerCommand {
  forceStart(0x01),
  forceStop(0x02),
  clearSchedule(0x03);

  final int value;

  const ChargerCommand(this.value);

  List<int> toBytes() => [value];
}
