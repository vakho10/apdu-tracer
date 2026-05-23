import { contextBridge, ipcRenderer } from 'electron'
import type { AppSettings } from '../main/settings'

export type { AppSettings, ThemePreference, ViewPreference } from '../main/settings'

export interface ApduRecord {
  direction: 'command' | 'response'
  hex: string
  device: string // USB device address, used to pair commands per reader
}

export interface StatusMessage {
  kind: 'info' | 'error'
  text: string
}

export interface TraceEntry {
  time: string
  direction: 'command' | 'response'
  hex: string
}

export interface ExportResult {
  saved: boolean
  path?: string
  error?: string
}

export interface ImportResult {
  imported: boolean
  path?: string
  apdus?: TraceEntry[]
  error?: string
}

export interface OpenCaptureResult {
  started: boolean
  path?: string
  error?: string
}

export interface TracerApi {
  listInterfaces(): Promise<{ name: string; description: string }[]>
  startTrace(iface: string): void
  stopTrace(): void
  onApdu(cb: (record: ApduRecord) => void): void
  onAtr(cb: (hex: string) => void): void
  onStatus(cb: (message: StatusMessage) => void): void
  onStopped(cb: () => void): void
  getSettings(): Promise<AppSettings>
  updateSettings(patch: Partial<AppSettings>): Promise<AppSettings>
  exportTrace(entries: TraceEntry[]): Promise<ExportResult>
  importTrace(): Promise<ImportResult>
  openCaptureFile(): Promise<OpenCaptureResult>
  locateTshark(): Promise<string | null>
}

const api: TracerApi = {
  listInterfaces: () => ipcRenderer.invoke('tracer:list'),
  startTrace: (iface) => ipcRenderer.send('tracer:start', iface),
  stopTrace: () => ipcRenderer.send('tracer:stop'),
  onApdu: (cb) => ipcRenderer.on('tracer:apdu', (_e, record: ApduRecord) => cb(record)),
  onAtr: (cb) => ipcRenderer.on('tracer:atr', (_e, hex: string) => cb(hex)),
  onStatus: (cb) => ipcRenderer.on('tracer:status', (_e, message: StatusMessage) => cb(message)),
  onStopped: (cb) => ipcRenderer.on('tracer:stopped', () => cb()),
  getSettings: () => ipcRenderer.invoke('settings:get'),
  updateSettings: (patch) => ipcRenderer.invoke('settings:update', patch),
  exportTrace: (entries) => ipcRenderer.invoke('trace:export', entries),
  importTrace: () => ipcRenderer.invoke('trace:import'),
  openCaptureFile: () => ipcRenderer.invoke('tracer:open-file'),
  locateTshark: () => ipcRenderer.invoke('tracer:locate-tshark')
}

contextBridge.exposeInMainWorld('api', api)
