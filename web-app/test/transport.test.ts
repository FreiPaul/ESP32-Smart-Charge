import { afterEach, describe, expect, it, vi } from 'vitest'
import {
  BluetoothUnavailableError,
  isBluetoothAvailable,
  WebBluetoothTransport,
} from '../src/transport'

interface StubOptions {
  withResponse?: boolean
  failService?: boolean
  failNotifications?: boolean
}

class StubCharacteristic extends EventTarget {
  value: DataView | undefined
  readonly writes: Uint8Array[] = []
  readonly writeCalls: string[] = []
  notifying = false

  constructor(
    private readonly failNotifications: boolean,
    withResponse: boolean,
  ) {
    super()
    if (withResponse) {
      Object.assign(this, {
        writeValueWithResponse: (data: BufferSource) => this.record('withResponse', data),
      })
    }
  }

  async readValue(): Promise<DataView> {
    this.value = new DataView(new Uint8Array([0x01]).buffer)
    return this.value
  }

  async writeValue(data: BufferSource): Promise<void> {
    this.record('plain', data)
  }

  async startNotifications(): Promise<StubCharacteristic> {
    if (this.failNotifications) {
      throw new Error('subscription refused')
    }
    this.notifying = true
    return this
  }

  push(packet: Uint8Array): void {
    this.value = new DataView(packet.buffer)
    this.dispatchEvent(new Event('characteristicvaluechanged'))
  }

  private record(kind: string, data: BufferSource): void {
    this.writeCalls.push(kind)
    this.writes.push(
      data instanceof ArrayBuffer
        ? new Uint8Array(data)
        : new Uint8Array(data.buffer, data.byteOffset, data.byteLength),
    )
  }
}

class StubDevice extends EventTarget {
  readonly name = 'ESP-CHARGER'
  disconnectCalls = 0
  readonly gatt = {
    connected: false,
    connect: async () => {
      this.gatt.connected = true
      return {
        getPrimaryService: async () => {
          if (this.options.failService) {
            throw new Error('service discovery failed')
          }
          return { getCharacteristic: async (uuid: string) => this.characteristic(uuid) }
        },
      }
    },
    disconnect: () => {
      this.disconnectCalls++
      if (!this.gatt.connected) return
      this.gatt.connected = false
      this.dispatchEvent(new Event('gattserverdisconnected'))
    },
  }

  private readonly characteristics = new Map<string, StubCharacteristic>()

  constructor(private readonly options: StubOptions) {
    super()
  }

  characteristic(uuid: string): StubCharacteristic {
    let existing = this.characteristics.get(uuid)
    if (!existing) {
      existing = new StubCharacteristic(
        this.options.failNotifications === true && uuid.endsWith('def3'),
        this.options.withResponse !== false,
      )
      this.characteristics.set(uuid, existing)
    }
    return existing
  }
}

function installBluetooth(options: StubOptions = {}): StubDevice {
  const device = new StubDevice(options)
  Object.defineProperty(globalThis, 'navigator', {
    configurable: true,
    value: { bluetooth: { requestDevice: async () => device } },
  })
  return device
}

function removeBluetooth(): void {
  Object.defineProperty(globalThis, 'navigator', { configurable: true, value: {} })
}

afterEach(() => {
  Object.defineProperty(globalThis, 'navigator', { configurable: true, value: {} })
})

const STATUS_UUID = '12345678-1234-5678-1234-56789abcdef3'
const COMMAND_UUID = '12345678-1234-5678-1234-56789abcdef4'

describe('WebBluetoothTransport', () => {
  it('reports a browser without Web Bluetooth instead of connecting', async () => {
    removeBluetooth()

    expect(isBluetoothAvailable()).toBe(false)
    await expect(new WebBluetoothTransport().connect()).rejects.toBeInstanceOf(
      BluetoothUnavailableError,
    )
  })

  it('writes with response where the browser offers it', async () => {
    const device = installBluetooth({ withResponse: true })
    const transport = new WebBluetoothTransport()
    await transport.connect()

    await transport.writeCommand(new Uint8Array([0x01]))

    expect(device.characteristic(COMMAND_UUID).writeCalls).toEqual(['withResponse'])
  })

  it('falls back to writeValue on an older Web Bluetooth implementation', async () => {
    const device = installBluetooth({ withResponse: false })
    const transport = new WebBluetoothTransport()
    await transport.connect()

    await transport.writeCommand(new Uint8Array([0x02]))

    expect(device.characteristic(COMMAND_UUID).writeCalls).toEqual(['plain'])
    expect(Array.from(device.characteristic(COMMAND_UUID).writes[0] ?? [])).toEqual([0x02])
  })

  it('stays usable when the device refuses the status subscription', async () => {
    installBluetooth({ failNotifications: true })
    const transport = new WebBluetoothTransport()

    await transport.connect()

    expect((await transport.readStatus()).getUint8(0)).toBe(0x01)
  })

  it('drops a half-open link when service discovery fails', async () => {
    const device = installBluetooth({ failService: true })
    const transport = new WebBluetoothTransport()
    const disconnected = vi.fn()
    transport.onDisconnect(disconnected)

    await expect(transport.connect()).rejects.toThrow(/service discovery/)

    expect(device.gatt.connected).toBe(false)
    expect(device.disconnectCalls).toBe(1)
    expect(disconnected).toHaveBeenCalledTimes(1)
  })

  it('stops listening to a device it has already dropped', async () => {
    const device = installBluetooth({ failService: true })
    const transport = new WebBluetoothTransport()
    const disconnected = vi.fn()
    transport.onDisconnect(disconnected)
    await expect(transport.connect()).rejects.toThrow(/service discovery/)

    device.dispatchEvent(new Event('gattserverdisconnected'))

    expect(disconnected).toHaveBeenCalledTimes(1)
  })

  it('forwards status notifications and a device-side disconnect', async () => {
    const device = installBluetooth()
    const transport = new WebBluetoothTransport()
    const packets: number[][] = []
    const disconnected = vi.fn()
    transport.onStatus((packet) => packets.push([packet.getUint8(0), packet.getUint8(1)]))
    transport.onDisconnect(disconnected)
    await transport.connect()

    device.characteristic(STATUS_UUID).push(new Uint8Array([0x01, 0x02]))
    device.gatt.disconnect()

    expect(packets).toEqual([[0x01, 0x02]])
    expect(disconnected).toHaveBeenCalledTimes(1)
  })
})
