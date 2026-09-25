import { describe, expect, it, vi } from 'vitest'
import { ChargerClient, nextOccurrence } from '../src/client'
import { ChargerState, FLAG_SCHEDULE_ENABLED, FLAG_THRESHOLD_ENABLED } from '../src/protocol'
import { FakeTransport } from './fake-transport'
import { encodeStatus } from './status-packet'

function clockAt(iso: string): () => Date {
  return () => new Date(iso)
}

describe('nextOccurrence', () => {
  it('keeps a time that is still ahead on the same day', () => {
    const start = nextOccurrence({ hour: 14, minute: 0 }, new Date('2026-03-04T09:30:00'))

    expect(start).toEqual(new Date('2026-03-04T14:00:00'))
  })

  it('moves a time that already passed to the next day', () => {
    const start = nextOccurrence({ hour: 8, minute: 15 }, new Date('2026-03-04T09:30:00'))

    expect(start).toEqual(new Date('2026-03-05T08:15:00'))
  })

  it('keeps a time that is exactly now, which starts the charge immediately', () => {
    const start = nextOccurrence({ hour: 9, minute: 30 }, new Date('2026-03-04T09:30:00'))

    expect(start).toEqual(new Date('2026-03-04T09:30:00'))
  })
})

describe('ChargerClient.uploadSettings', () => {
  it('sends the next occurrence of the selected time', async () => {
    const transport = new FakeTransport()
    const client = new ChargerClient(transport, clockAt('2026-03-04T09:30:00'))

    await client.uploadSettings({
      schedule: { hour: 8, minute: 15 },
      thresholdEnabled: true,
      voltageThresholdMv: 40500,
    })

    const packet = transport.settingsWrites[0]
    expect(packet).toBeDefined()
    const view = new DataView(packet!.buffer)
    expect(view.getUint32(1, true)).toBe(Math.floor(new Date('2026-03-04T09:30:00').getTime() / 1000))
    expect(view.getUint8(5)).toBe(FLAG_THRESHOLD_ENABLED | FLAG_SCHEDULE_ENABLED)
    expect(view.getUint32(6, true)).toBe(
      Math.floor(new Date('2026-03-05T08:15:00').getTime() / 1000),
    )
  })

  it('clears the schedule flag when no time is selected', async () => {
    const transport = new FakeTransport()
    const client = new ChargerClient(transport, clockAt('2026-03-04T09:30:00'))

    await client.uploadSettings({
      schedule: null,
      thresholdEnabled: false,
      voltageThresholdMv: 40500,
    })

    const view = new DataView(transport.settingsWrites[0]!.buffer)
    expect(view.getUint8(5)).toBe(0)
    expect(view.getUint32(6, true)).toBe(0)
  })
})

describe('ChargerClient commands', () => {
  it('maps the manual controls onto the command bytes', async () => {
    const transport = new FakeTransport()
    const client = new ChargerClient(transport)

    await client.forceStart()
    await client.forceStop()

    expect(transport.commandWrites.map((packet) => packet[0])).toEqual([0x01, 0x02])
  })
})

describe('ChargerClient status stream', () => {
  const status = {
    state: ChargerState.Charging,
    voltageMv: 39800,
    deviceTime: 1_700_000_000,
    relayOn: true,
    thresholdCountdown: 0,
    errorCode: 0,
    scheduledStartTime: 0,
    voltageThresholdMv: 40500,
    scheduleEnabled: false,
    thresholdEnabled: true,
  }

  it('decodes notifications for subscribers', () => {
    const transport = new FakeTransport()
    const client = new ChargerClient(transport)
    const listener = vi.fn()
    client.onStatus(listener)

    transport.emitStatus(encodeStatus(status))

    expect(listener).toHaveBeenCalledWith(status)
  })

  it('drops a malformed notification instead of tearing down the stream', () => {
    const transport = new FakeTransport()
    const client = new ChargerClient(transport)
    const listener = vi.fn()
    client.onStatus(listener)

    transport.emitStatus(new Uint8Array([0x01, 0x02]))
    transport.emitStatus(encodeStatus(status))

    expect(listener).toHaveBeenCalledTimes(1)
    expect(listener).toHaveBeenCalledWith(status)
  })
})
