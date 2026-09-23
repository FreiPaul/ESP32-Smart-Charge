export const SERVICE_UUID = '12345678-1234-5678-1234-56789abcdef0'
export const SETTINGS_CHAR_UUID = '12345678-1234-5678-1234-56789abcdef2'
export const STATUS_CHAR_UUID = '12345678-1234-5678-1234-56789abcdef3'
export const COMMAND_CHAR_UUID = '12345678-1234-5678-1234-56789abcdef4'

export const DEVICE_NAME = 'ESP-CHARGER'

export const PROTOCOL_VERSION = 0x01
export const SETTINGS_PACKET_SIZE = 14
export const STATUS_PACKET_SIZE = 18

export const FLAG_THRESHOLD_ENABLED = 1 << 0
export const FLAG_SCHEDULE_ENABLED = 1 << 1

export const THRESHOLD_DELAY_SECONDS = 10

export enum ChargerState {
  Idle = 0,
  Scheduled = 1,
  Charging = 2,
  ThresholdDelay = 3,
  Complete = 4,
  Error = 5,
}

export enum ChargerCommand {
  ForceStart = 0x01,
  ForceStop = 0x02,
  ClearSchedule = 0x03,
}

export interface ChargerSettings {
  currentTime: number
  scheduleEnabled: boolean
  scheduledStartTime: number
  thresholdEnabled: boolean
  voltageThresholdMv: number
}

export interface ChargerStatus {
  state: ChargerState
  voltageMv: number
  deviceTime: number
  relayOn: boolean
  thresholdCountdown: number
  errorCode: number
  scheduledStartTime: number
  voltageThresholdMv: number
  scheduleEnabled: boolean
  thresholdEnabled: boolean
}

export const STATE_LABELS: Record<ChargerState, string> = {
  [ChargerState.Idle]: 'Idle',
  [ChargerState.Scheduled]: 'Scheduled',
  [ChargerState.Charging]: 'Charging',
  [ChargerState.ThresholdDelay]: 'Stopping...',
  [ChargerState.Complete]: 'Complete',
  [ChargerState.Error]: 'Error',
}

function xorChecksum(bytes: Uint8Array, end: number): number {
  let checksum = 0
  for (let i = 0; i < end; i++) {
    checksum ^= bytes[i] as number
  }
  return checksum
}

export function encodeSettings(settings: ChargerSettings): Uint8Array {
  const packet = new Uint8Array(SETTINGS_PACKET_SIZE)
  const view = new DataView(packet.buffer)

  let flags = 0
  if (settings.thresholdEnabled) flags |= FLAG_THRESHOLD_ENABLED
  if (settings.scheduleEnabled) flags |= FLAG_SCHEDULE_ENABLED

  view.setUint8(0, PROTOCOL_VERSION)
  view.setUint32(1, settings.currentTime, true)
  view.setUint8(5, flags)
  view.setUint32(6, settings.scheduleEnabled ? settings.scheduledStartTime : 0, true)
  view.setUint16(10, settings.voltageThresholdMv, true)
  view.setUint8(12, 0)
  view.setUint8(13, xorChecksum(packet, SETTINGS_PACKET_SIZE - 1))

  return packet
}

export function encodeCommand(command: ChargerCommand): Uint8Array {
  return new Uint8Array([command])
}

export function decodeStatus(data: DataView | Uint8Array | ArrayBuffer): ChargerStatus {
  const view =
    data instanceof DataView
      ? data
      : data instanceof Uint8Array
        ? new DataView(data.buffer, data.byteOffset, data.byteLength)
        : new DataView(data)

  if (view.byteLength < STATUS_PACKET_SIZE) {
    throw new Error(
      `Invalid status packet: expected ${STATUS_PACKET_SIZE} bytes, got ${view.byteLength}`,
    )
  }

  const version = view.getUint8(0)
  if (version !== PROTOCOL_VERSION) {
    throw new Error(`Unsupported status version: expected ${PROTOCOL_VERSION}, got ${version}`)
  }

  const flags = view.getUint8(17)

  return {
    state: view.getUint8(1) as ChargerState,
    voltageMv: view.getUint16(2, true),
    deviceTime: view.getUint32(4, true),
    relayOn: view.getUint8(8) !== 0,
    thresholdCountdown: view.getUint8(9),
    errorCode: view.getUint8(10),
    scheduledStartTime: view.getUint32(11, true),
    voltageThresholdMv: view.getUint16(15, true),
    scheduleEnabled: (flags & FLAG_SCHEDULE_ENABLED) !== 0,
    thresholdEnabled: (flags & FLAG_THRESHOLD_ENABLED) !== 0,
  }
}

export function emptyStatus(): ChargerStatus {
  return {
    state: ChargerState.Idle,
    voltageMv: 0,
    deviceTime: 0,
    relayOn: false,
    thresholdCountdown: 0,
    errorCode: 0,
    scheduledStartTime: 0,
    voltageThresholdMv: 40500,
    scheduleEnabled: false,
    thresholdEnabled: false,
  }
}
