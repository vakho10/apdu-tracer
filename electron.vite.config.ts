import { defineConfig } from 'electron-vite'

// electron-vite auto-detects entry points:
//   main    -> src/main/index.ts
//   preload -> src/preload/index.ts
//   renderer-> src/renderer/index.html
export default defineConfig({
  main: {},
  preload: {},
  renderer: {}
})
