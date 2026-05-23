import { app, BrowserWindow, dialog, ipcMain, nativeTheme } from 'electron'
import { join } from 'node:path'
import { existsSync, readFileSync, writeFileSync } from 'node:fs'
import { spawn, execFile, type ChildProcess } from 'node:child_process'
import * as settings from './settings'
import type { AppSettings } from './settings'
import type { ExportResult, ImportResult, OpenCaptureResult, TraceEntry } from '../preload'

/** CCID message types that carry an APDU payload. */
const CCID_XFR_BLOCK = '6f' // PC -> RDR : command APDU
const CCID_DATA_BLOCK = '80' // RDR -> PC : response APDU
const CCID_ICC_POWER_ON = '62' // PC -> RDR : power on; the next DataBlock holds the ATR

/** Bytes of the CCID message header that precede the APDU (abData). */
const CCID_HEADER_BYTES = 10

const TSHARK_CANDIDATES = [
  'C:\\Program Files\\Wireshark\\tshark.exe',
  'C:\\Program Files (x86)\\Wireshark\\tshark.exe'
]

let mainWindow: BrowserWindow | null = null
let tracer: ChildProcess | null = null
let pendingAtr = false // set by an IccPowerOn, consumed by the following DataBlock

function tsharkPath(): string {
  const configured = settings.get().tsharkPath
  if (configured && existsSync(configured)) return configured
  return TSHARK_CANDIDATES.find((p) => existsSync(p)) ?? 'tshark'
}

/** Preload is emitted as either .mjs or .js depending on the electron-vite version. */
function preloadPath(): string {
  const mjs = join(__dirname, '../preload/index.mjs')
  return existsSync(mjs) ? mjs : join(__dirname, '../preload/index.js')
}

function iconPath(): string {
  return app.isPackaged
    ? join(process.resourcesPath, 'icon.png')
    : join(__dirname, '../../build/icon.png')
}

function createWindow(): void {
  mainWindow = new BrowserWindow({
    width: 760,
    height: 580,
    minWidth: 480,
    minHeight: 360,
    title: 'APDU Tracer',
    icon: iconPath(),
    backgroundColor: nativeTheme.shouldUseDarkColors ? '#1e1e2e' : '#f5f5f7',
    webPreferences: {
      preload: preloadPath(),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false
    }
  })

  mainWindow.removeMenu()

  const rendererUrl = process.env['ELECTRON_RENDERER_URL']
  if (rendererUrl) mainWindow.loadURL(rendererUrl)
  else mainWindow.loadFile(join(__dirname, '../renderer/index.html'))

  mainWindow.on('closed', () => {
    mainWindow = null
  })
}

function sendStatus(kind: 'info' | 'error', text: string): void {
  mainWindow?.webContents.send('tracer:status', { kind, text })
}

/**
 * Lists USBPcap capture interfaces reported by `tshark -D`.
 * USBPcap captures per host-controller (USBPcap1, USBPcap2, ...), not per port.
 */
function listInterfaces(): Promise<{ name: string; description: string }[]> {
  return new Promise((resolve, reject) => {
    execFile(tsharkPath(), ['-D'], { windowsHide: true }, (err, stdout) => {
      if (err) {
        reject(new Error(err.message))
        return
      }
      const items: { name: string; description: string }[] = []
      for (const line of stdout.split(/\r?\n/)) {
        if (!/usbpcap/i.test(line)) continue
        // e.g. "16. \\.\USBPcap1 (USBPcap1)"
        const m = line.match(/^\s*\d+\.\s+(.+?)(?:\s+\(([^)]+)\))?\s*$/)
        if (!m) continue
        const name = m[2] ?? m[1]
        items.push({ name, description: m[2] ?? m[1] })
      }
      resolve(items)
    })
  })
}

/** A CCID DataBlock (0x80) is a response; an XfrBlock (0x6f) is a command. */
function isResponse(messageType: string, usbDirection: string): boolean {
  const t = messageType.trim().toLowerCase().replace(/^0x/, '')
  if (t === CCID_DATA_BLOCK || t === '128') return true
  if (t === CCID_XFR_BLOCK || t === '111') return false
  // Fallback: USB endpoint direction (1 = IN = device -> host = response).
  return usbDirection.trim() === '1'
}

/** Parses a single `tshark -T fields` line and forwards an APDU to the renderer. */
function handleLine(line: string): void {
  if (!line.trim()) return
  const [messageType = '', usbDirection = '', abData = '', capData = '', deviceAddress = ''] =
    line.split('|')
  const type = messageType.trim().toLowerCase().replace(/^0x/, '')

  // An IccPowerOn carries no APDU; it just marks the next DataBlock as the ATR.
  if (type === CCID_ICC_POWER_ON || type === '98') {
    pendingAtr = true
    return
  }

  let hex = abData.replace(/[^0-9a-fA-F]/g, '')
  if (!hex && capData) {
    // Fallback: capdata holds the whole CCID message; drop the header.
    hex = capData.replace(/[^0-9a-fA-F]/g, '').slice(CCID_HEADER_BYTES * 2)
  }

  const response = isResponse(messageType, usbDirection)

  // The DataBlock right after an IccPowerOn is the ATR, not an APDU response.
  if (response && pendingAtr) {
    pendingAtr = false
    if (hex) mainWindow?.webContents.send('tracer:atr', hex.toUpperCase())
    return
  }
  if (!response) pendingAtr = false // an XfrBlock cancels a stale pending ATR

  if (!hex) return
  mainWindow?.webContents.send('tracer:apdu', {
    direction: response ? 'response' : 'command',
    hex: hex.toUpperCase(),
    device: deviceAddress.trim()
  })
}

/** Streams APDUs from a live USBPcap interface or a saved capture file. */
function startCapture(source: { iface: string } | { file: string }): void {
  if (tracer) return
  pendingAtr = false

  const input = 'iface' in source ? ['-i', source.iface] : ['-r', source.file]
  const label = 'iface' in source ? `interface ${source.iface}` : source.file

  const args = [
    ...input,
    '-l', // flush stdout after every packet
    '-n', // no name resolution (faster)
    '-Y',
    `usbccid.bMessageType == 0x${CCID_XFR_BLOCK} || usbccid.bMessageType == 0x${CCID_DATA_BLOCK}`
      + ` || usbccid.bMessageType == 0x${CCID_ICC_POWER_ON}`,
    '-T', 'fields',
    '-E', 'separator=|',
    '-e', 'usbccid.bMessageType',
    '-e', 'usb.endpoint_address.direction',
    '-e', 'data.data',
    '-e', 'usb.capdata',
    '-e', 'usb.device_address'
  ]

  let proc: ChildProcess
  try {
    proc = spawn(tsharkPath(), args, { windowsHide: true })
  } catch (err) {
    sendStatus('error', `Failed to launch tshark: ${(err as Error).message}`)
    return
  }
  tracer = proc
  sendStatus('info', `Reading APDUs from ${label}…`)

  let buffer = ''
  proc.stdout?.setEncoding('utf8')
  proc.stdout?.on('data', (chunk: string) => {
    buffer += chunk
    const lines = buffer.split(/\r?\n/)
    buffer = lines.pop() ?? ''
    for (const line of lines) handleLine(line)
  })

  proc.stderr?.setEncoding('utf8')
  proc.stderr?.on('data', (chunk: string) => {
    const text = chunk.trim()
    if (text) sendStatus('info', text)
  })

  proc.on('error', (err) => {
    sendStatus('error', `tshark error: ${err.message}`)
    tracer = null
    mainWindow?.webContents.send('tracer:stopped')
  })

  proc.on('close', (code) => {
    if (code && code !== 0) sendStatus('error', `tshark exited with code ${code}`)
    else sendStatus('info', 'Tracing stopped.')
    tracer = null
    mainWindow?.webContents.send('tracer:stopped')
  })
}

function stopTracing(): void {
  if (!tracer?.pid) {
    tracer = null
    return
  }
  // taskkill /T also terminates the dumpcap child that tshark spawns.
  if (process.platform === 'win32') {
    spawn('taskkill', ['/pid', String(tracer.pid), '/T', '/F'], { windowsHide: true })
  } else {
    tracer.kill()
  }
}

/** Prompts for a .pcap/.pcapng file and replays its APDUs through tshark. */
async function openCaptureFile(): Promise<OpenCaptureResult> {
  if (!mainWindow) return { started: false }
  if (tracer) return { started: false, error: 'A capture is already running.' }

  const { canceled, filePaths } = await dialog.showOpenDialog(mainWindow, {
    title: 'Open capture file',
    filters: [{ name: 'Capture files', extensions: ['pcapng', 'pcap'] }],
    properties: ['openFile']
  })
  if (canceled || filePaths.length === 0) return { started: false }

  startCapture({ file: filePaths[0] })
  return { started: true, path: filePaths[0] }
}

/** Prompts the user to pick tshark.exe and persists it for future captures. */
async function locateTshark(): Promise<string | null> {
  if (!mainWindow) return null

  const { canceled, filePaths } = await dialog.showOpenDialog(mainWindow, {
    title: 'Locate tshark.exe',
    filters: [
      { name: 'tshark', extensions: ['exe'] },
      { name: 'All files', extensions: ['*'] }
    ],
    properties: ['openFile']
  })
  if (canceled || filePaths.length === 0) return null

  settings.update({ tsharkPath: filePaths[0] })
  return filePaths[0]
}

function exportFileName(): string {
  const now = new Date()
  const pad = (n: number): string => String(n).padStart(2, '0')
  const stamp = `${now.getFullYear()}${pad(now.getMonth() + 1)}${pad(now.getDate())}`
    + `-${pad(now.getHours())}${pad(now.getMinutes())}${pad(now.getSeconds())}`
  return `apdu-trace-${stamp}.txt`
}

function traceAsText(entries: TraceEntry[]): string {
  return entries
    .map((entry) => {
      const arrow = entry.direction === 'command' ? '>>' : '<<'
      const hex = entry.hex.replace(/(..)(?=.)/g, '$1 ').toUpperCase()
      return `[${entry.time}] ${arrow} ${hex}`
    })
    .join('\n')
}

function traceAsJson(entries: TraceEntry[]): string {
  return JSON.stringify(
    { tool: 'APDU Tracer', exportedAt: new Date().toISOString(), count: entries.length, apdus: entries },
    null,
    2
  )
}

function csvField(value: string): string {
  return /[",\r\n]/.test(value) ? `"${value.replace(/"/g, '""')}"` : value
}

function traceAsCsv(entries: TraceEntry[]): string {
  const rows = ['Time,Direction,Hex']
  for (const entry of entries) {
    rows.push([entry.time, entry.direction, entry.hex].map(csvField).join(','))
  }
  return rows.join('\r\n')
}

async function exportTrace(entries: TraceEntry[]): Promise<ExportResult> {
  if (!mainWindow) return { saved: false }

  const { canceled, filePath } = await dialog.showSaveDialog(mainWindow, {
    title: 'Save APDU trace',
    defaultPath: exportFileName(),
    filters: [
      { name: 'Text file', extensions: ['txt'] },
      { name: 'JSON file', extensions: ['json'] },
      { name: 'CSV file', extensions: ['csv'] }
    ]
  })
  if (canceled || !filePath) return { saved: false }

  try {
    const lower = filePath.toLowerCase()
    const content = lower.endsWith('.json')
      ? traceAsJson(entries)
      : lower.endsWith('.csv')
        ? traceAsCsv(entries)
        : traceAsText(entries)
    writeFileSync(filePath, content, 'utf8')
    return { saved: true, path: filePath }
  } catch (err) {
    return { saved: false, error: (err as Error).message }
  }
}

/**
 * Validates a parsed JSON document as a trace produced by `traceAsJson`.
 * Returns the APDU entries, or null when the shape is not recognized.
 */
function parseImported(data: unknown): TraceEntry[] | null {
  if (typeof data !== 'object' || data === null) return null
  const apdus = (data as { apdus?: unknown }).apdus
  if (!Array.isArray(apdus)) return null

  const out: TraceEntry[] = []
  for (const item of apdus) {
    if (typeof item !== 'object' || item === null) return null
    const { time, direction, hex } = item as Record<string, unknown>
    if (direction !== 'command' && direction !== 'response') return null
    if (typeof hex !== 'string' || !/^[0-9a-fA-F]{2,}$/.test(hex)) return null
    out.push({ time: typeof time === 'string' ? time : '', direction, hex: hex.toUpperCase() })
  }
  return out.length > 0 ? out : null
}

async function importTrace(): Promise<ImportResult> {
  if (!mainWindow) return { imported: false }

  const { canceled, filePaths } = await dialog.showOpenDialog(mainWindow, {
    title: 'Import APDU trace',
    filters: [{ name: 'JSON file', extensions: ['json'] }],
    properties: ['openFile']
  })
  if (canceled || filePaths.length === 0) return { imported: false }

  const filePath = filePaths[0]
  try {
    const apdus = parseImported(JSON.parse(readFileSync(filePath, 'utf8')))
    if (!apdus) return { imported: false, error: 'File is not a recognized APDU trace.' }
    return { imported: true, path: filePath, apdus }
  } catch (err) {
    return { imported: false, error: (err as Error).message }
  }
}

ipcMain.handle('tracer:list', () => listInterfaces())
ipcMain.on('tracer:start', (_e, iface: string) => startCapture({ iface }))
ipcMain.on('tracer:stop', () => stopTracing())
ipcMain.handle('tracer:open-file', () => openCaptureFile())
ipcMain.handle('tracer:locate-tshark', () => locateTshark())

ipcMain.handle('settings:get', () => settings.get())
ipcMain.handle('settings:update', (_e, patch: Partial<AppSettings>) => settings.update(patch))
ipcMain.handle('trace:export', (_e, entries: TraceEntry[]) => exportTrace(entries))
ipcMain.handle('trace:import', () => importTrace())

app.whenReady().then(() => {
  settings.load()
  createWindow()
  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow()
  })
})

app.on('window-all-closed', () => {
  stopTracing()
  if (process.platform !== 'darwin') app.quit()
})

app.on('before-quit', () => stopTracing())
