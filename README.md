# ESP32-C6 BLE LED Controller

A minimal demonstration of Bluetooth Low Energy communication between an ESP32-C6 microcontroller and an iOS app.

## Overview

This project demonstrates:

- BLE GATT server implementation on ESP32-C6 using ESP-IDF (Bluedroid stack)
- BLE client implementation in Flutter for iOS
- Simple LED control over Bluetooth as demonatration usecase

## Hardware

- **Board**: Seeed Studio XIAO ESP32-C6
- **LED**: Internal LED

## BLE Service

| UUID                                   | Description                    |
| -------------------------------------- | ------------------------------ |
| `12345678-1234-5678-1234-56789abcdef0` | LED Control Service            |
| `12345678-1234-5678-1234-56789abcdef1` | LED State Characteristic (R/W) |

**LED State**: `0x00` = OFF, `0x01` = ON

## Building

### ESP32 Firmware

```bash
# activate the esp-idf, e.g.:
. ~/esp/esp-idf/export.sh
cd esp-proj
idf.py set-target esp32c6
idf.py build
idf.py flash monitor
```

### iOS App

```bash
cd mobile-app
flutter pub get
open ios/Runner.xcworkspace
# Build and run on physical iOS device (BLE requires real hardware)
# then you can later also use
flutter run --release
```

## Usage

1. Power on ESP32-C6 — it will start advertising as "ESP-LED-CTRL"
2. Open the iOS app and tap "Scan for Devices"
3. Select "ESP-LED-CTRL" from the list
4. Use the toggle button to control the LED

## Requirements

- ESP-IDF v5.x or v6.x
- Flutter 3.x
- iOS 13.0+
- Physical iOS device (BLE not available in simulator)
