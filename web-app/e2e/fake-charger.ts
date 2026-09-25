export interface FakeChargerSnapshot {
  state: number
  relayOn: boolean
  deviceTime: number
  scheduledStart: number
  thresholdMv: number
  flags: number
  settingsWrites: number
  rejectedWrites: number
}

export interface FakeChargerHandle {
  setVoltage(millivolts: number): void
  tick(seconds?: number): void
  disconnect(): void
  snapshot(): FakeChargerSnapshot
}

/**
 * Mirrors esp-proj/main/charger_fsm.c and its byte protocol.
 *
 * Serialized by Playwright's addInitScript, so it must stay self-contained.
 */
export function installFakeCharger(): void {
  const SERVICE_UUID = '12345678-1234-5678-1234-56789abcdef0'
  const SETTINGS_CHAR_UUID = '12345678-1234-5678-1234-56789abcdef2'
  const STATUS_CHAR_UUID = '12345678-1234-5678-1234-56789abcdef3'
  const COMMAND_CHAR_UUID = '12345678-1234-5678-1234-56789abcdef4'

  const FLAG_THRESHOLD_ENABLED = 1 << 0
  const FLAG_SCHEDULE_ENABLED = 1 << 1
  const THRESHOLD_DELAY_SECONDS = 10

  const STATE_IDLE = 0
  const STATE_SCHEDULED = 1
  const STATE_CHARGING = 2
  const STATE_THRESHOLD_DELAY = 3
  const STATE_COMPLETE = 4

  const charger = {
    state: STATE_IDLE,
    relayOn: false,
    voltageMv: 0,
    deviceTime: 0,
    timeSynced: false,
    scheduledStart: 0,
    thresholdMv: 0,
    flags: 0,
    countdown: 0,
    settingsWrites: 0,
    rejectedWrites: 0,
  }

  function statusPacket(): Uint8Array {
    const packet = new Uint8Array(18)
    const view = new DataView(packet.buffer)
    view.setUint8(0, 0x01)
    view.setUint8(1, charger.state)
    view.setUint16(2, charger.voltageMv, true)
    view.setUint32(4, charger.deviceTime, true)
    view.setUint8(8, charger.relayOn ? 1 : 0)
    view.setUint8(9, charger.countdown)
    view.setUint8(10, 0)
    view.setUint32(11, charger.scheduledStart, true)
    view.setUint16(15, charger.thresholdMv, true)
    view.setUint8(17, charger.flags)
    return packet
  }

  function applySettings(bytes: Uint8Array): void {
    if (bytes.length < 14) {
      charger.rejectedWrites++
      return
    }
    let checksum = 0
    for (let i = 0; i < 13; i++) {
      checksum ^= bytes[i] as number
    }
    if (checksum !== bytes[13] || bytes[0] !== 0x01) {
      charger.rejectedWrites++
      return
    }

    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
    charger.deviceTime = view.getUint32(1, true)
    charger.timeSynced = true
    charger.flags = view.getUint8(5)
    charger.scheduledStart = view.getUint32(6, true)
    charger.thresholdMv = view.getUint16(10, true)
    charger.countdown = 0
    charger.settingsWrites++

    if (charger.flags & FLAG_SCHEDULE_ENABLED) {
      if (charger.scheduledStart <= charger.deviceTime) {
        charger.relayOn = true
        charger.state = STATE_CHARGING
      } else {
        charger.relayOn = false
        charger.state = STATE_SCHEDULED
      }
    } else {
      charger.relayOn = false
      charger.state = STATE_IDLE
    }
  }

  function processCommand(command: number): void {
    if (command === 0x01) {
      charger.relayOn = true
      charger.countdown = 0
      charger.state = STATE_CHARGING
    } else if (command === 0x02) {
      charger.relayOn = false
      charger.countdown = 0
      charger.flags &= ~FLAG_SCHEDULE_ENABLED
      charger.state = STATE_IDLE
    } else if (command === 0x03) {
      charger.flags &= ~FLAG_SCHEDULE_ENABLED
      charger.countdown = 0
      if (charger.state === STATE_SCHEDULED) {
        charger.state = STATE_IDLE
      }
    }
  }

  function reconcile(): void {
    if (charger.timeSynced) {
      charger.deviceTime++
    }

    if (charger.state === STATE_SCHEDULED) {
      if (charger.timeSynced && charger.deviceTime >= charger.scheduledStart) {
        charger.relayOn = true
        charger.state = STATE_CHARGING
      }
    } else if (charger.state === STATE_CHARGING) {
      if (charger.flags & FLAG_THRESHOLD_ENABLED && charger.voltageMv >= charger.thresholdMv) {
        charger.countdown = THRESHOLD_DELAY_SECONDS
        charger.state = STATE_THRESHOLD_DELAY
      }
    } else if (charger.state === STATE_THRESHOLD_DELAY) {
      if (charger.voltageMv < charger.thresholdMv) {
        charger.countdown = 0
        charger.state = STATE_CHARGING
      } else {
        charger.countdown--
        if (charger.countdown === 0) {
          charger.relayOn = false
          charger.flags &= ~FLAG_SCHEDULE_ENABLED
          charger.state = STATE_COMPLETE
        }
      }
    }
  }

  class FakeCharacteristic extends EventTarget {
    value: DataView | undefined
    private notifying = false

    constructor(readonly uuid: string) {
      super()
    }

    async readValue(): Promise<DataView> {
      const packet = statusPacket()
      this.value = new DataView(packet.buffer)
      return this.value
    }

    async writeValue(data: BufferSource): Promise<void> {
      const bytes =
        data instanceof ArrayBuffer ? new Uint8Array(data) : new Uint8Array(data.buffer, data.byteOffset, data.byteLength)
      if (this.uuid === SETTINGS_CHAR_UUID) {
        applySettings(bytes)
      } else if (this.uuid === COMMAND_CHAR_UUID && bytes.length >= 1) {
        processCommand(bytes[0] as number)
      }
      notifyStatus()
    }

    async writeValueWithResponse(data: BufferSource): Promise<void> {
      await this.writeValue(data)
    }

    async startNotifications(): Promise<FakeCharacteristic> {
      this.notifying = true
      return this
    }

    push(packet: Uint8Array): void {
      if (!this.notifying) return
      this.value = new DataView(packet.buffer)
      this.dispatchEvent(new Event('characteristicvaluechanged'))
    }
  }

  const statusCharacteristic = new FakeCharacteristic(STATUS_CHAR_UUID)
  const characteristics = new Map<string, FakeCharacteristic>([
    [SETTINGS_CHAR_UUID, new FakeCharacteristic(SETTINGS_CHAR_UUID)],
    [STATUS_CHAR_UUID, statusCharacteristic],
    [COMMAND_CHAR_UUID, new FakeCharacteristic(COMMAND_CHAR_UUID)],
  ])

  function notifyStatus(): void {
    statusCharacteristic.push(statusPacket())
  }

  const service = {
    uuid: SERVICE_UUID,
    async getCharacteristic(uuid: string): Promise<FakeCharacteristic> {
      const characteristic = characteristics.get(uuid.toLowerCase())
      if (!characteristic) {
        throw new Error(`No characteristic ${uuid}`)
      }
      return characteristic
    },
  }

  class FakeDevice extends EventTarget {
    readonly name = 'ESP-CHARGER'
    readonly id = 'fake-charger'
    readonly gatt = {
      connected: false,
      connect: async () => {
        this.gatt.connected = true
        return {
          getPrimaryService: async (uuid: string) => {
            if (uuid.toLowerCase() !== SERVICE_UUID) {
              throw new Error(`No service ${uuid}`)
            }
            return service
          },
        }
      },
      disconnect: () => {
        if (!this.gatt.connected) return
        this.gatt.connected = false
        this.dispatchEvent(new Event('gattserverdisconnected'))
      },
    }
  }

  const device = new FakeDevice()

  Object.defineProperty(navigator, 'bluetooth', {
    configurable: true,
    value: {
      requestDevice: async () => device,
      getAvailability: async () => true,
    },
  })

  const handle: FakeChargerHandle = {
    setVoltage(millivolts) {
      charger.voltageMv = millivolts
      notifyStatus()
    },
    tick(seconds = 1) {
      for (let i = 0; i < seconds; i++) {
        reconcile()
      }
      notifyStatus()
    },
    disconnect() {
      device.gatt.disconnect()
    },
    snapshot() {
      return {
        state: charger.state,
        relayOn: charger.relayOn,
        deviceTime: charger.deviceTime,
        scheduledStart: charger.scheduledStart,
        thresholdMv: charger.thresholdMv,
        flags: charger.flags,
        settingsWrites: charger.settingsWrites,
        rejectedWrites: charger.rejectedWrites,
      }
    },
  }

  ;(window as unknown as { fakeCharger: FakeChargerHandle }).fakeCharger = handle
}
