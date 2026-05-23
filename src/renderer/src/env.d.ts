import type { TracerApi } from '../../preload'

declare global {
  interface Window {
    api: TracerApi
  }
}

export {}
