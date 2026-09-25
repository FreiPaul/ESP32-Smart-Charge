import {
  ChargerCommand,
  type ChargerStatus,
  decodeStatus,
  encodeCommand,
  encodeSettings,
} from './protocol'
import type { ChargerTransport } from './transport'

export interface ScheduleSelection {
  hour: number
  minute: number
}

export interface UploadRequest {
  schedule: ScheduleSelection | null
  thresholdEnabled: boolean
  voltageThresholdMv: number
}

/** The firmware compares against an absolute instant, not a wall-clock time. */
export function nextOccurrence(schedule: ScheduleSelection, now: Date): Date {
  const scheduled = new Date(
    now.getFullYear(),
    now.getMonth(),
    now.getDate(),
    schedule.hour,
    schedule.minute,
    0,
    0,
  )
  if (scheduled.getTime() < now.getTime()) {
    scheduled.setDate(scheduled.getDate() + 1)
  }
  return scheduled
}

function unixSeconds(date: Date): number {
  return Math.floor(date.getTime() / 1000)
}

export class ChargerClient {
  private statusListeners: ((status: ChargerStatus) => void)[] = []

  constructor(
    private readonly transport: ChargerTransport,
    private readonly clock: () => Date = () => new Date(),
  ) {
    this.transport.onStatus((packet) => {
      let status: ChargerStatus
      try {
        status = decodeStatus(packet)
      } catch {
        return
      }
      for (const listener of this.statusListeners) {
        listener(status)
      }
    })
  }

  async connect(): Promise<void> {
    await this.transport.connect()
  }

  async disconnect(): Promise<void> {
    await this.transport.disconnect()
  }

  async readStatus(): Promise<ChargerStatus> {
    return decodeStatus(await this.transport.readStatus())
  }

  async uploadSettings(request: UploadRequest): Promise<void> {
    const now = this.clock()
    const scheduledStart = request.schedule ? nextOccurrence(request.schedule, now) : null

    await this.transport.writeSettings(
      encodeSettings({
        currentTime: unixSeconds(now),
        scheduleEnabled: scheduledStart !== null,
        scheduledStartTime: scheduledStart ? unixSeconds(scheduledStart) : 0,
        thresholdEnabled: request.thresholdEnabled,
        voltageThresholdMv: request.voltageThresholdMv,
      }),
    )
  }

  async forceStart(): Promise<void> {
    await this.transport.writeCommand(encodeCommand(ChargerCommand.ForceStart))
  }

  async forceStop(): Promise<void> {
    await this.transport.writeCommand(encodeCommand(ChargerCommand.ForceStop))
  }

  onStatus(listener: (status: ChargerStatus) => void): void {
    this.statusListeners.push(listener)
  }

  onDisconnect(listener: () => void): void {
    this.transport.onDisconnect(listener)
  }
}
