import { describe, expect, it } from 'vitest'
import {
  ChargerCommand,
  ChargerState,
  decodeStatus,
  encodeCommand,
  encodeSettings,
  FLAG_SCHEDULE_ENABLED,
  FLAG_THRESHOLD_ENABLED,
  PROTOCOL_VERSION,
  SETTINGS_PACKET_SIZE,
  STATUS_PACKET_SIZE,
} from '../src/protocol'
import { encodeStatus } from './status-packet'

describe('encodeSettings', () => {
  const settings = {
    currentTime: 0x66778899,
    scheduleEnabled: true,
    scheduledStartTime: 0x11223344,
    thresholdEnabled: true,
    voltageThresholdMv: 40500,
  }

  it('lays the fields out the way the firmware parses them', () => {
    const packet = encodeSettings(settings)

    expect(packet).toHaveLength(SETTINGS_PACKET_SIZE)
    expect(packet[0]).toBe(PROTOCOL_VERSION)
    expect(Array.from(packet.slice(1, 5))).toEqual([0x99, 0x88, 0x77, 0x66])
    expect(packet[5]).toBe(FLAG_THRESHOLD_ENABLED | FLAG_SCHEDULE_ENABLED)
    expect(Array.from(packet.slice(6, 10))).toEqual([0x44, 0x33, 0x22, 0x11])
    expect(Array.from(packet.slice(10, 12))).toEqual([0x34, 0x9e])
    expect(packet[12]).toBe(0)
  })

  it('appends the checksum the firmware verifies', () => {
    const packet = encodeSettings(settings)

    expect(packet[13]).toBe(0xec)
  })

  it('clears the flags and the start time when nothing is scheduled', () => {
    const packet = encodeSettings({
      ...settings,
      scheduleEnabled: false,
      thresholdEnabled: false,
    })

    expect(packet[5]).toBe(0)
    expect(Array.from(packet.slice(6, 10))).toEqual([0, 0, 0, 0])
    expect(packet[13]).toBe(0xab)
  })
})

describe('decodeStatus', () => {
  const status = {
    state: ChargerState.ThresholdDelay,
    voltageMv: 41234,
    deviceTime: 1_700_000_000,
    relayOn: true,
    thresholdCountdown: 7,
    errorCode: 0,
    scheduledStartTime: 1_700_003_600,
    voltageThresholdMv: 40500,
    scheduleEnabled: true,
    thresholdEnabled: true,
  }

  it('reads the field offsets the firmware writes', () => {
    // status_serialize in esp-proj/main/charger_fsm.c, field by field.
    const packet = new Uint8Array([
      0x01, 0x03, 0x12, 0xa1, 0x00, 0xf1, 0x53, 0x65, 0x01, 0x07, 0x00, 0x10, 0xff, 0x53, 0x65,
      0x34, 0x9e, 0x03,
    ])

    expect(decodeStatus(packet)).toEqual(status)
  })

  it('reads back every field of a status packet', () => {
    expect(decodeStatus(encodeStatus(status))).toEqual(status)
  })

  it('reads a DataView straight from a BLE characteristic', () => {
    const packet = encodeStatus(status)
    const view = new DataView(packet.buffer, packet.byteOffset, packet.byteLength)

    expect(decodeStatus(view)).toEqual(status)
  })

  it('rejects a truncated packet', () => {
    const packet = encodeStatus(status).slice(0, STATUS_PACKET_SIZE - 1)

    expect(() => decodeStatus(packet)).toThrow(/expected 18 bytes/)
  })

  it('rejects a packet from an unknown protocol version', () => {
    const packet = encodeStatus(status)
    packet[0] = 0x02

    expect(() => decodeStatus(packet)).toThrow(/version/)
  })
})

describe('encodeCommand', () => {
  it('sends a single command byte', () => {
    expect(Array.from(encodeCommand(ChargerCommand.ForceStop))).toEqual([0x02])
  })
})
