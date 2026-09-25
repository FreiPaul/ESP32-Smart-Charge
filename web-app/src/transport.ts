import {
  COMMAND_CHAR_UUID,
  DEVICE_NAME,
  SERVICE_UUID,
  SETTINGS_CHAR_UUID,
  STATUS_CHAR_UUID,
} from './protocol'

export interface ChargerTransport {
  connect(): Promise<void>
  disconnect(): Promise<void>
  readStatus(): Promise<DataView>
  writeSettings(packet: Uint8Array): Promise<void>
  writeCommand(packet: Uint8Array): Promise<void>
  onStatus(listener: (packet: DataView) => void): void
  onDisconnect(listener: () => void): void
}

export class BluetoothUnavailableError extends Error {
  constructor() {
    super('Web Bluetooth is not available in this browser.')
    this.name = 'BluetoothUnavailableError'
  }
}

export function isBluetoothAvailable(): boolean {
  return typeof navigator !== 'undefined' && navigator.bluetooth !== undefined
}

export class WebBluetoothTransport implements ChargerTransport {
  private device: BluetoothDevice | null = null
  private settingsChar: BluetoothRemoteGATTCharacteristic | null = null
  private statusChar: BluetoothRemoteGATTCharacteristic | null = null
  private commandChar: BluetoothRemoteGATTCharacteristic | null = null
  private statusListeners: ((packet: DataView) => void)[] = []
  private disconnectListeners: (() => void)[] = []

  async connect(): Promise<void> {
    if (!isBluetoothAvailable()) {
      throw new BluetoothUnavailableError()
    }

    this.device = await navigator.bluetooth.requestDevice({
      filters: [{ services: [SERVICE_UUID] }, { namePrefix: DEVICE_NAME }],
      optionalServices: [SERVICE_UUID],
    })

    this.device.addEventListener('gattserverdisconnected', this.handleDisconnect)

    try {
      const server = await this.device.gatt?.connect()
      if (!server) {
        throw new Error('Device has no GATT server')
      }

      const service = await server.getPrimaryService(SERVICE_UUID)
      this.settingsChar = await service.getCharacteristic(SETTINGS_CHAR_UUID)
      this.statusChar = await service.getCharacteristic(STATUS_CHAR_UUID)
      this.commandChar = await service.getCharacteristic(COMMAND_CHAR_UUID)

      this.statusChar.addEventListener('characteristicvaluechanged', this.handleStatusChange)
      try {
        await this.statusChar.startNotifications()
      } catch {
        // Status stays reachable through readStatus, so a device that refuses
        // the subscription is still usable.
      }
    } catch (error) {
      // A half-open link keeps the charger from advertising again, so the
      // device has to be dropped before the failure reaches the caller.
      this.device?.gatt?.disconnect()
      this.handleDisconnect()
      throw error
    }
  }

  async disconnect(): Promise<void> {
    this.device?.gatt?.disconnect()
    this.handleDisconnect()
  }

  async readStatus(): Promise<DataView> {
    if (!this.statusChar) {
      throw new Error('Not connected')
    }
    return await this.statusChar.readValue()
  }

  async writeSettings(packet: Uint8Array): Promise<void> {
    await this.write(this.settingsChar, packet)
  }

  async writeCommand(packet: Uint8Array): Promise<void> {
    await this.write(this.commandChar, packet)
  }

  onStatus(listener: (packet: DataView) => void): void {
    this.statusListeners.push(listener)
  }

  onDisconnect(listener: () => void): void {
    this.disconnectListeners.push(listener)
  }

  private async write(
    characteristic: BluetoothRemoteGATTCharacteristic | null,
    packet: Uint8Array,
  ): Promise<void> {
    if (!characteristic) {
      throw new Error('Not connected')
    }
    // Bluefy ships an older Web Bluetooth surface than desktop Chrome and does
    // not expose the explicit-response variants on every build.
    if (typeof characteristic.writeValueWithResponse === 'function') {
      await characteristic.writeValueWithResponse(packet as BufferSource)
    } else {
      await characteristic.writeValue(packet as BufferSource)
    }
  }

  private handleStatusChange = (event: Event): void => {
    const target = event.target as BluetoothRemoteGATTCharacteristic
    const value = target.value
    if (!value) return
    for (const listener of this.statusListeners) {
      listener(value)
    }
  }

  private handleDisconnect = (): void => {
    if (!this.device) return
    this.device.removeEventListener('gattserverdisconnected', this.handleDisconnect)
    this.statusChar?.removeEventListener('characteristicvaluechanged', this.handleStatusChange)
    this.device = null
    this.settingsChar = null
    this.statusChar = null
    this.commandChar = null
    for (const listener of this.disconnectListeners) {
      listener()
    }
  }
}
