import { expect, type Page, test } from '@playwright/test'
import { type FakeChargerHandle, type FakeChargerSnapshot, installFakeCharger } from './fake-charger'

declare global {
  interface Window {
    fakeCharger: FakeChargerHandle
  }
}

function snapshot(page: Page): Promise<FakeChargerSnapshot> {
  return page.evaluate(() => window.fakeCharger.snapshot())
}

function tick(page: Page, seconds: number): Promise<void> {
  return page.evaluate((count) => window.fakeCharger.tick(count), seconds)
}

function setVoltage(page: Page, millivolts: number): Promise<void> {
  return page.evaluate((mv) => window.fakeCharger.setVoltage(mv), millivolts)
}

async function setRange(page: Page, selector: string, value: string): Promise<void> {
  await page.locator(selector).evaluate((node, next) => {
    const input = node as HTMLInputElement
    input.value = next
    input.dispatchEvent(new Event('input', { bubbles: true }))
  }, value)
}

async function connect(page: Page): Promise<void> {
  await page.getByRole('button', { name: 'Connect Charger' }).click()
  await expect(page.locator('#app-title')).toHaveText('Charger Settings')
}

test.beforeEach(async ({ page }) => {
  await page.addInitScript(installFakeCharger)
  await page.goto('/')
})

test('drives a scheduled charge from pairing to completion', async ({ page }) => {
  await expect(page.locator('#app-title')).toHaveText('Find Charger')

  await connect(page)
  await expect(page.locator('#status-state')).toHaveText('Idle')
  await expect(page.locator('#status-relay')).toHaveText('Relay OFF')

  await page.locator('#schedule-time').fill('14:00')
  await expect(page.locator('#schedule-hint')).toContainText('Charging will start at 14:00')

  await page.locator('#threshold-enabled').check()
  await setRange(page, '#threshold-value', '40.5')
  await expect(page.locator('#threshold-label')).toHaveText('40.5 V')

  await page.getByRole('button', { name: 'Upload Settings' }).click()
  await expect(page.locator('#toast')).toHaveText('Settings saved')
  await expect(page.locator('#status-state')).toHaveText('Scheduled')

  const scheduled = await snapshot(page)
  expect(scheduled.thresholdMv).toBe(40500)
  expect(scheduled.scheduledStart).toBeGreaterThan(scheduled.deviceTime)

  await tick(page, scheduled.scheduledStart - scheduled.deviceTime)
  await expect(page.locator('#status-state')).toHaveText('Charging')
  await expect(page.locator('#status-relay')).toHaveText('Relay ON')

  await setVoltage(page, 40_600)
  await expect(page.locator('#status-voltage')).toHaveText('40.60 V')

  await tick(page, 1)
  await expect(page.locator('#status-state')).toHaveText('Stopping...')
  await expect(page.locator('#countdown-label')).toHaveText('Stopping in 10s...')

  await tick(page, 10)
  await expect(page.locator('#status-state')).toHaveText('Complete')
  await expect(page.locator('#status-relay')).toHaveText('Relay OFF')
  await expect(page.locator('#countdown')).toBeHidden()
})

test('a voltage dip during the countdown resumes charging', async ({ page }) => {
  await connect(page)

  await page.locator('#threshold-enabled').check()
  await setRange(page, '#threshold-value', '40.5')
  await page.getByRole('button', { name: 'Upload Settings' }).click()
  await expect(page.locator('#toast')).toHaveText('Settings saved')

  await page.getByRole('button', { name: 'Start Now' }).click()
  await expect(page.locator('#status-state')).toHaveText('Charging')

  await setVoltage(page, 40_600)
  await tick(page, 3)
  await expect(page.locator('#status-state')).toHaveText('Stopping...')

  await setVoltage(page, 39_900)
  await tick(page, 1)
  await expect(page.locator('#status-state')).toHaveText('Charging')
  await expect(page.locator('#countdown')).toBeHidden()
})

test('manual stop overrides the schedule', async ({ page }) => {
  await connect(page)

  await page.locator('#schedule-time').fill('06:30')
  await page.getByRole('button', { name: 'Upload Settings' }).click()
  await expect(page.locator('#status-state')).toHaveText('Scheduled')

  await page.getByRole('button', { name: 'Start Now' }).click()
  await expect(page.locator('#status-state')).toHaveText('Charging')
  await expect(page.locator('#status-relay')).toHaveText('Relay ON')

  await page.getByRole('button', { name: 'Stop' }).click()
  await expect(page.locator('#status-state')).toHaveText('Idle')
  await expect(page.locator('#status-relay')).toHaveText('Relay OFF')
})

test('the threshold slider only applies while the switch is on', async ({ page }) => {
  await connect(page)

  await expect(page.locator('#threshold-controls')).toBeHidden()
  await page.locator('#threshold-enabled').check()
  await expect(page.locator('#threshold-controls')).toBeVisible()

  await setRange(page, '#threshold-value', '41.2')
  await page.locator('#threshold-enabled').uncheck()
  await expect(page.locator('#threshold-controls')).toBeHidden()

  await page.getByRole('button', { name: 'Upload Settings' }).click()
  await expect(page.locator('#toast')).toHaveText('Settings saved')

  const state = await snapshot(page)
  expect(state.flags & 0b1).toBe(0)
  expect(state.rejectedWrites).toBe(0)
})

test('reconnecting seeds the form from the device', async ({ page }) => {
  await connect(page)

  await page.locator('#schedule-time').fill('14:00')
  await page.locator('#threshold-enabled').check()
  await setRange(page, '#threshold-value', '41.0')
  await page.getByRole('button', { name: 'Upload Settings' }).click()
  await expect(page.locator('#status-state')).toHaveText('Scheduled')

  await page.getByRole('button', { name: 'Disconnect' }).click()
  await expect(page.locator('#app-title')).toHaveText('Find Charger')

  await connect(page)
  await expect(page.locator('#schedule-time')).toHaveValue('14:00')
  await expect(page.locator('#threshold-enabled')).toBeChecked()
  await expect(page.locator('#threshold-label')).toHaveText('41.0 V')
})

test('a device-side disconnect returns to the pairing screen', async ({ page }) => {
  await connect(page)

  await page.evaluate(() => window.fakeCharger.disconnect())

  await expect(page.locator('#app-title')).toHaveText('Find Charger')
  await expect(page.locator('#toast')).toHaveText('Device disconnected')
})

test('a refused device chooser leaves the pairing screen with a reason', async ({ page }) => {
  await page.addInitScript(() => {
    Object.defineProperty(navigator, 'bluetooth', {
      configurable: true,
      value: {
        requestDevice: () => Promise.reject(new Error('User cancelled the requestDevice() chooser')),
      },
    })
  })
  await page.reload()

  await page.getByRole('button', { name: 'Connect Charger' }).click()

  await expect(page.locator('#app-title')).toHaveText('Find Charger')
  await expect(page.locator('#pairing-error')).toContainText('cancelled')
  await expect(page.getByRole('button', { name: 'Connect Charger' })).toBeEnabled()
})
