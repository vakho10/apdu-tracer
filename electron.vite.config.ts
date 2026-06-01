import { resolve } from 'node:path'
import { defineConfig } from 'electron-vite'

// electron-vite auto-detects entry points:
//   main    -> src/main/index.ts
//   preload -> src/preload/index.ts
//   renderer-> src/renderer/index.html
// The renderer build is given explicit inputs so the splash window's HTML is
// emitted alongside the main window's index.html.
export default defineConfig({
  main: {},
  preload: {},
  renderer: {
    build: {
      rollupOptions: {
        input: {
          index: resolve(__dirname, 'src/renderer/index.html'),
          splash: resolve(__dirname, 'src/renderer/splash.html')
        }
      }
    }
  }
})
