import { app, nativeTheme } from 'electron'
import { join } from 'node:path'
import { readFileSync, writeFileSync } from 'node:fs'

export type ThemePreference = 'system' | 'light' | 'dark'
export type ViewPreference = 'simple' | 'detailed' | 'summary' | 'decode'

export interface AppSettings {
  theme: ThemePreference
  autoScroll: boolean
  view: ViewPreference
  tsharkPath: string // explicit tshark.exe path; '' means auto-detect
}

const DEFAULTS: AppSettings = {
  theme: 'system',
  autoScroll: true,
  view: 'simple',
  tsharkPath: ''
}

let current: AppSettings = { ...DEFAULTS }

function settingsFile(): string {
  return join(app.getPath('userData'), 'settings.json')
}

function applyTheme(): void {
  nativeTheme.themeSource = current.theme
}

/** Reads persisted settings from disk and applies the saved theme. Call once, after app is ready. */
export function load(): void {
  try {
    const raw = JSON.parse(readFileSync(settingsFile(), 'utf8')) as Partial<AppSettings>
    current = { ...DEFAULTS, ...raw }
  } catch {
    current = { ...DEFAULTS }
  }
  applyTheme()
}

export function get(): AppSettings {
  return { ...current }
}

export function update(patch: Partial<AppSettings>): AppSettings {
  current = { ...current, ...patch }
  applyTheme()
  try {
    writeFileSync(settingsFile(), JSON.stringify(current, null, 2))
  } catch {
    // Persistence is best-effort; ignore write failures.
  }
  return get()
}
