import { defineConfig, devices } from '@playwright/test'

const PORT = 4173

// Pointing this at a running container checks the served image, not just a
// local build.
const baseURL = process.env.E2E_BASE_URL ?? `http://127.0.0.1:${PORT}`

export default defineConfig({
  testDir: './e2e',
  fullyParallel: true,
  forbidOnly: !!process.env.CI,
  retries: process.env.CI ? 1 : 0,
  reporter: process.env.CI ? [['html', { open: 'never' }]] : [['list']],
  use: {
    baseURL,
    trace: 'on-first-retry',
  },
  projects: [
    // The app targets Bluefy on iPhone, so the suite drives a phone viewport.
    { name: 'mobile', use: { ...devices['iPhone 14'], browserName: 'chromium' } },
  ],
  webServer: process.env.E2E_BASE_URL
    ? undefined
    : {
        command: `npm run build && npx vite preview --host 127.0.0.1 --port ${PORT} --strictPort`,
        url: `http://127.0.0.1:${PORT}`,
        reuseExistingServer: !process.env.CI,
        timeout: 120_000,
      },
})
