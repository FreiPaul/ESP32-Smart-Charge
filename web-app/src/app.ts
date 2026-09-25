import { type ChargerClient, nextOccurrence, type ScheduleSelection } from './client'
import { type ChargerStatus, emptyStatus, STATE_LABELS, THRESHOLD_DELAY_SECONDS } from './protocol'

const DEFAULT_THRESHOLD_V = 40.5

function el<T extends HTMLElement>(id: string): T {
  const node = document.getElementById(id)
  if (!node) {
    throw new Error(`Missing element #${id}`)
  }
  return node as T
}

function parseTime(value: string): ScheduleSelection | null {
  const match = /^(\d{1,2}):(\d{2})(?::\d{2})?$/.exec(value)
  if (!match) return null
  return { hour: Number(match[1]), minute: Number(match[2]) }
}

function formatTime(schedule: ScheduleSelection): string {
  const hour = String(schedule.hour).padStart(2, '0')
  const minute = String(schedule.minute).padStart(2, '0')
  return `${hour}:${minute}`
}

export function mountApp(client: ChargerClient, clock: () => Date = () => new Date()): void {
  const title = el('app-title')
  const pairing = el('pairing')
  const pairingError = el('pairing-error')
  const settings = el('settings')
  const connectButton = el<HTMLButtonElement>('connect')
  const disconnectButton = el<HTMLButtonElement>('disconnect')

  const statusIcon = el('status-icon')
  const statusState = el('status-state')
  const statusRelay = el('status-relay')
  const statusVoltage = el('status-voltage')
  const countdown = el('countdown')
  const countdownBar = el<HTMLProgressElement>('countdown-bar')
  const countdownLabel = el('countdown-label')

  const scheduleInput = el<HTMLInputElement>('schedule-time')
  const scheduleClear = el<HTMLButtonElement>('schedule-clear')
  const scheduleHint = el('schedule-hint')

  const thresholdEnabled = el<HTMLInputElement>('threshold-enabled')
  const thresholdControls = el('threshold-controls')
  const thresholdValue = el<HTMLInputElement>('threshold-value')
  const thresholdLabel = el('threshold-label')

  const uploadButton = el<HTMLButtonElement>('upload')
  const forceStartButton = el<HTMLButtonElement>('force-start')
  const forceStopButton = el<HTMLButtonElement>('force-stop')

  const toast = el('toast')
  let toastTimer: ReturnType<typeof setTimeout> | undefined

  function showToast(message: string, kind: 'success' | 'error' | 'warning'): void {
    toast.textContent = message
    toast.className = kind === 'success' ? 'toast' : `toast ${kind}`
    toast.hidden = false
    clearTimeout(toastTimer)
    toastTimer = setTimeout(() => {
      toast.hidden = true
    }, 4000)
  }

  function renderStatus(status: ChargerStatus): void {
    statusState.textContent = STATE_LABELS[status.state] ?? 'Unknown'
    statusState.dataset.state = String(status.state)
    statusRelay.textContent = status.relayOn ? 'Relay ON' : 'Relay OFF'
    statusIcon.textContent = status.relayOn ? '⚡' : '⏻'
    statusIcon.className = status.relayOn ? 'status-icon charging' : 'status-icon'
    statusVoltage.textContent = `${(status.voltageMv / 1000).toFixed(2)} V`

    countdown.hidden = status.thresholdCountdown === 0
    countdownBar.max = THRESHOLD_DELAY_SECONDS
    countdownBar.value = status.thresholdCountdown
    countdownLabel.textContent = `Stopping in ${status.thresholdCountdown}s...`
  }

  function readSchedule(): ScheduleSelection | null {
    if (scheduleInput.value === '') {
      return null
    }
    const schedule = parseTime(scheduleInput.value)
    if (!schedule) {
      throw new Error(`Unrecognised start time: ${scheduleInput.value}`)
    }
    return schedule
  }

  function renderSchedule(): void {
    const schedule = parseTime(scheduleInput.value)
    scheduleClear.hidden = schedule === null
    if (!schedule) {
      scheduleHint.hidden = true
      return
    }
    const now = clock()
    const start = nextOccurrence(schedule, now)
    const day = start.getDate() === now.getDate() ? 'today' : 'tomorrow'
    scheduleHint.hidden = false
    scheduleHint.textContent = `Charging will start at ${formatTime(schedule)} (${day})`
  }

  function renderThreshold(): void {
    thresholdControls.hidden = !thresholdEnabled.checked
    thresholdLabel.textContent = `${Number(thresholdValue.value).toFixed(1)} V`
  }

  function showPairing(): void {
    title.textContent = 'Find Charger'
    pairing.hidden = false
    settings.hidden = true
    disconnectButton.hidden = true
    connectButton.disabled = false
    connectButton.textContent = 'Connect Charger'
  }

  function showSettings(): void {
    title.textContent = 'Charger Settings'
    pairing.hidden = true
    pairingError.hidden = true
    settings.hidden = false
    disconnectButton.hidden = false
  }

  function seedForm(status: ChargerStatus): void {
    if (status.scheduledStartTime > 0 && status.scheduleEnabled) {
      const start = new Date(status.scheduledStartTime * 1000)
      scheduleInput.value = formatTime({ hour: start.getHours(), minute: start.getMinutes() })
    } else {
      scheduleInput.value = ''
    }
    thresholdEnabled.checked = status.thresholdEnabled
    thresholdValue.value = String(
      status.voltageThresholdMv === 0 ? DEFAULT_THRESHOLD_V : status.voltageThresholdMv / 1000,
    )
    renderSchedule()
    renderThreshold()
  }

  connectButton.addEventListener('click', async () => {
    connectButton.disabled = true
    connectButton.textContent = 'Connecting...'
    pairingError.hidden = true
    try {
      await client.connect()
      showSettings()
      try {
        const status = await client.readStatus()
        renderStatus(status)
        seedForm(status)
      } catch {
        // Status arrives through notifications if the initial read is refused.
      }
    } catch (error) {
      pairingError.hidden = false
      pairingError.textContent = error instanceof Error ? error.message : String(error)
      showPairing()
    }
  })

  disconnectButton.addEventListener('click', () => {
    void client.disconnect()
  })

  scheduleInput.addEventListener('input', renderSchedule)

  scheduleClear.addEventListener('click', () => {
    scheduleInput.value = ''
    renderSchedule()
  })

  thresholdEnabled.addEventListener('change', renderThreshold)
  thresholdValue.addEventListener('input', renderThreshold)

  uploadButton.addEventListener('click', async () => {
    uploadButton.disabled = true
    uploadButton.textContent = 'Uploading...'
    try {
      await client.uploadSettings({
        schedule: readSchedule(),
        thresholdEnabled: thresholdEnabled.checked,
        voltageThresholdMv: Math.round(Number(thresholdValue.value) * 1000),
      })
      showToast('Settings saved', 'success')
    } catch (error) {
      showToast(`Failed to save settings: ${describe(error)}`, 'error')
    } finally {
      uploadButton.disabled = false
      uploadButton.textContent = 'Upload Settings'
    }
  })

  forceStartButton.addEventListener('click', async () => {
    try {
      await client.forceStart()
    } catch (error) {
      showToast(`Failed: ${describe(error)}`, 'error')
    }
  })

  forceStopButton.addEventListener('click', async () => {
    try {
      await client.forceStop()
    } catch (error) {
      showToast(`Failed: ${describe(error)}`, 'error')
    }
  })

  client.onStatus(renderStatus)
  client.onDisconnect(() => {
    showPairing()
    showToast('Device disconnected', 'warning')
  })

  renderStatus(emptyStatus())
  thresholdValue.value = String(DEFAULT_THRESHOLD_V)
  renderSchedule()
  renderThreshold()
  showPairing()
}

function describe(error: unknown): string {
  return error instanceof Error ? error.message : String(error)
}
