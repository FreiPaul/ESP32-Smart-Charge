import { type ChargerStatus, FLAG_SCHEDULE_ENABLED, FLAG_THRESHOLD_ENABLED, PROTOCOL_VERSION, STATUS_PACKET_SIZE } from '../src/protocol'

/** Counterpart of status_serialize in esp-proj/main/charger_fsm.c. */
export function encodeStatus(status: ChargerStatus): Uint8Array {
  const packet = new Uint8Array(STATUS_PACKET_SIZE)
  const view = new DataView(packet.buffer)

  let flags = 0
  if (status.thresholdEnabled) flags |= FLAG_THRESHOLD_ENABLED
  if (status.scheduleEnabled) flags |= FLAG_SCHEDULE_ENABLED

  view.setUint8(0, PROTOCOL_VERSION)
  view.setUint8(1, status.state)
  view.setUint16(2, status.voltageMv, true)
  view.setUint32(4, status.deviceTime, true)
  view.setUint8(8, status.relayOn ? 1 : 0)
  view.setUint8(9, status.thresholdCountdown)
  view.setUint8(10, status.errorCode)
  view.setUint32(11, status.scheduledStartTime, true)
  view.setUint16(15, status.voltageThresholdMv, true)
  view.setUint8(17, flags)

  return packet
}
