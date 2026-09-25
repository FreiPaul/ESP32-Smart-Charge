import type { ChargerTransport } from '../src/transport'

export class FakeTransport implements ChargerTransport {
  readonly settingsWrites: Uint8Array[] = []
  readonly commandWrites: Uint8Array[] = []
  statusPacket = new Uint8Array(18)

  private statusListeners: ((packet: DataView) => void)[] = []
  private disconnectListeners: (() => void)[] = []

  async connect(): Promise<void> {}

  async disconnect(): Promise<void> {
    for (const listener of this.disconnectListeners) listener()
  }

  async readStatus(): Promise<DataView> {
    return new DataView(this.statusPacket.buffer.slice(0))
  }

  async writeSettings(packet: Uint8Array): Promise<void> {
    this.settingsWrites.push(packet)
  }

  async writeCommand(packet: Uint8Array): Promise<void> {
    this.commandWrites.push(packet)
  }

  onStatus(listener: (packet: DataView) => void): void {
    this.statusListeners.push(listener)
  }

  onDisconnect(listener: () => void): void {
    this.disconnectListeners.push(listener)
  }

  emitStatus(packet: Uint8Array): void {
    const view = new DataView(packet.buffer, packet.byteOffset, packet.byteLength)
    for (const listener of this.statusListeners) listener(view)
  }
}
