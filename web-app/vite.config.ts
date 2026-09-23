import { defineConfig } from 'vitest/config'
import { viteSingleFile } from 'vite-plugin-singlefile'

export default defineConfig({
  // Bluefy loads the app from whatever static host it is given, so the build
  // is inlined into one self-contained index.html with relative asset paths.
  base: './',
  plugins: [viteSingleFile()],
  build: {
    target: 'es2022',
    assetsInlineLimit: 100_000_000,
  },
  test: {
    globals: true,
    include: ['test/**/*.test.ts'],
  },
})
