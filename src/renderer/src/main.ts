import type { ApduRecord, StatusMessage, ThemePreference, TraceEntry, ViewPreference } from '../../preload'
import {
  byteHex,
  commandReturnsTlv,
  parseCommand,
  parseResponse,
  type CommandApdu,
  type MalformedApdu,
  type ResponseApdu,
  type StatusKind
} from './apdu'
import { detectProfile, parseTlv, type TlvNode, type TlvProfile } from './tlv'
import { parseAtr, type AtrInfo } from './atr'

const interfaceSelect = document.querySelector<HTMLSelectElement>('#interface')!
const refreshButton = document.querySelector<HTMLButtonElement>('#refresh')!
const tsharkButton = document.querySelector<HTMLButtonElement>('#tshark-path')!
const openFileButton = document.querySelector<HTMLButtonElement>('#open-file')!
const themeSelect = document.querySelector<HTMLSelectElement>('#theme')!
const toggleButton = document.querySelector<HTMLButtonElement>('#toggle')!
const autoScrollInput = document.querySelector<HTMLInputElement>('#autoscroll')!
const saveButton = document.querySelector<HTMLButtonElement>('#save')!
const importButton = document.querySelector<HTMLButtonElement>('#import')!
const clearButton = document.querySelector<HTMLButtonElement>('#clear')!
const counterElement = document.querySelector<HTMLElement>('#counter')!
const searchInput = document.querySelector<HTMLInputElement>('#search')!
const directionSelect = document.querySelector<HTMLSelectElement>('#filter-direction')!
const issuesInput = document.querySelector<HTMLInputElement>('#filter-issues')!
const profileSelect = document.querySelector<HTMLSelectElement>('#tlv-profile')!
const filterCount = document.querySelector<HTMLElement>('#filter-count')!
const output = document.querySelector<HTMLTextAreaElement>('#output')!
const cards = document.querySelector<HTMLDivElement>('#cards')!
const summary = document.querySelector<HTMLDivElement>('#summary')!
const decodeKindGroup = document.querySelector<HTMLElement>('.decode-kind')!
const decodeCommandRadio = document.querySelector<HTMLInputElement>('#decode-command')!
const decodeInput = document.querySelector<HTMLTextAreaElement>('#decode-input')!
const decodeResult = document.querySelector<HTMLDivElement>('#decode-result')!
const statusElement = document.querySelector<HTMLOutputElement>('#status')!
const tabList = document.querySelector<HTMLElement>('.tabs')!

const views: Record<ViewPreference, { tab: HTMLButtonElement; panel: HTMLElement }> = {
  simple: {
    tab: document.querySelector<HTMLButtonElement>('#tab-simple')!,
    panel: document.querySelector<HTMLElement>('#view-simple')!
  },
  detailed: {
    tab: document.querySelector<HTMLButtonElement>('#tab-detailed')!,
    panel: document.querySelector<HTMLElement>('#view-detailed')!
  },
  summary: {
    tab: document.querySelector<HTMLButtonElement>('#tab-summary')!,
    panel: document.querySelector<HTMLElement>('#view-summary')!
  },
  decode: {
    tab: document.querySelector<HTMLButtonElement>('#tab-decode')!,
    panel: document.querySelector<HTMLElement>('#view-decode')!
  }
}

const VIEW_ORDER: ViewPreference[] = ['simple', 'detailed', 'summary', 'decode']

interface PendingCommand {
  ins: number | null
  name: string
  startedAt: number | null // null when the command came from an imported trace
}

interface ResponsePairing {
  latencyMs: number
  commandIns: number | null
}

/** A measured command/response round-trip, used by the summary view. */
interface LatencySample {
  name: string
  ms: number
}

type DirectionFilter = 'all' | 'command' | 'response'

/** A captured APDU or meta line, rendered once and shown/hidden by the filter. */
interface RenderedEntry {
  line: string // simple-view text
  card: HTMLElement // detailed-view element
  search: string // lowercased text matched against the search query
  direction: 'command' | 'response' | null // null for meta lines
  status: StatusKind | null // response status, 'error' for malformed/error meta
  record?: TraceEntry // the exportable entry; absent for meta lines
  rebuild?: (profile: TlvProfile) => HTMLElement // re-renders the card for a profile
  captureProfile?: TlvProfile // the profile in effect when the APDU arrived
}

interface Filter {
  query: string
  direction: DirectionFilter
  issuesOnly: boolean
}

let tracing = false
let autoScroll = true
let activeView: ViewPreference = 'simple'
let commandCount = 0
let responseCount = 0
let profileChoice: 'auto' | TlvProfile = 'auto'
let detectedProfile: TlvProfile = 'iso'
let lastArrivedAt: number | null = null // arrival of the previous APDU, for time deltas
let lastIssueIndex = -1 // index in `entries` of the last jumped-to issue
/** Pending commands keyed by USB device address, so two readers do not cross-pair. */
const pendingByDevice = new Map<string, PendingCommand>()

const trace: TraceEntry[] = []
const entries: RenderedEntry[] = []
const latencies: LatencySample[] = []
const filter: Filter = { query: '', direction: 'all', issuesOnly: false }

const PROFILE_LABELS: Record<TlvProfile, string> = {
  iso: 'ISO 7816-4',
  emv: 'EMV',
  piv: 'PIV',
  openpgp: 'OpenPGP'
}

function timestamp(): string {
  return new Date().toLocaleTimeString('en-GB')
}

function scrollToBottom(): void {
  output.scrollTop = output.scrollHeight
  cards.scrollTop = cards.scrollHeight
}

/** "00A40400" -> "00 A4 04 00" */
function spaceHex(hex: string): string {
  return hex.replace(/(.{2})(?=.)/g, '$1 ')
}

/** Formats the millisecond gap since the previous APDU. */
function formatDelta(ms: number): string {
  return ms < 1000 ? `+${ms}ms` : `+${(ms / 1000).toFixed(1)}s`
}

// --- DOM helpers ------------------------------------------------------------

function el<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  className?: string,
  text?: string
): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag)
  if (className) node.className = className
  if (text !== undefined) node.textContent = text
  return node
}

function chip(label: string, value: string): HTMLElement {
  const node = el('span', 'chip')
  node.append(el('b', undefined, label), document.createTextNode(value))
  return node
}

function dataRow(label: string, hex: string): HTMLElement {
  const row = el('div', 'card-data')
  row.append(el('span', 'data-label', label), el('code', undefined, hex))
  return row
}

// --- Detailed view ----------------------------------------------------------

function cardHead(badge: string, badgeClass: string, title: string, titleClass: string, time: string): HTMLElement {
  const head = el('div', 'card-head')
  head.append(
    el('span', `badge ${badgeClass}`, badge),
    el('span', `card-title ${titleClass}`.trim(), title),
    el('time', undefined, time)
  )
  return head
}

function buildCommandCard(apdu: CommandApdu, time: string): HTMLElement {
  const card = el('article', 'card card--command')
  card.append(cardHead('Command', 'badge--command', apdu.name, '', time))

  const chips = el('div', 'chips')
  chips.append(
    chip('CLA', byteHex(apdu.cla)),
    chip('INS', byteHex(apdu.ins)),
    chip('P1', byteHex(apdu.p1)),
    chip('P2', byteHex(apdu.p2)),
    chip('Lc', apdu.lc === null ? '—' : String(apdu.lc)),
    chip('Le', apdu.le === null ? '—' : String(apdu.le)),
    chip('Case', String(apdu.apduCase))
  )
  if (apdu.secureMessaging) chips.append(chip('SM', apdu.secureMessaging))
  card.append(chips)

  if (apdu.data) card.append(dataRow('Data', apdu.data))
  if (apdu.note) card.append(el('p', 'card-note', apdu.note))
  return card
}

function renderTlvNode(node: TlvNode): HTMLElement {
  const header = el('div', 'tlv-header')
  header.append(el('span', 'tlv-tag', node.tag))
  if (node.name) header.append(el('span', 'tlv-name', node.name))
  header.append(el('span', 'tlv-len', `${node.length} B`))

  if (node.constructed) {
    const details = el('details', 'tlv-node tlv-constructed')
    details.open = true
    const summary = el('summary', 'tlv-summary')
    summary.append(header)
    details.append(summary)

    const childWrap = el('div', 'tlv-children')
    for (const child of node.children) childWrap.append(renderTlvNode(child))
    details.append(childWrap)
    return details
  }

  const row = el('div', 'tlv-node tlv-primitive')
  row.append(header)
  if (node.value) row.append(el('code', 'tlv-value', node.value))
  for (const line of node.decoded) row.append(el('span', 'tlv-decoded', `→ ${line}`))
  return row
}

function buildResponseCard(
  apdu: ResponseApdu,
  time: string,
  pairing: ResponsePairing | null,
  profile: TlvProfile
): HTMLElement {
  const card = el('article', 'card card--response')
  card.append(cardHead('Response', 'badge--response', apdu.meaning, `status-${apdu.status}`, time))

  const chips = el('div', 'chips')
  chips.append(chip('SW', `${byteHex(apdu.sw1)} ${byteHex(apdu.sw2)}`))
  if (pairing) chips.append(chip('Latency', `${pairing.latencyMs} ms`))
  card.append(chips)

  if (apdu.data) {
    card.append(dataRow('Data', apdu.data))
    // Decode as TLV only when the command is unknown or one that returns TLV.
    const tlvExpected = !pairing || pairing.commandIns === null || commandReturnsTlv(pairing.commandIns)
    if (tlvExpected) {
      const tlv = parseTlv(apdu.data, profile)
      if (tlv.ok) {
        const tree = el('div', 'tlv-tree')
        for (const node of tlv.nodes) tree.append(renderTlvNode(node))
        card.append(tree)
      }
    }
  }
  return card
}

function buildMalformedCard(record: ApduRecord, apdu: MalformedApdu, time: string): HTMLElement {
  const card = el('article', 'card card--malformed')
  const label = record.direction === 'command' ? 'Command' : 'Response'
  card.append(cardHead(label, 'badge--malformed', 'Malformed APDU', 'status-error', time))
  card.append(el('p', 'card-note', apdu.message))
  card.append(dataRow('Raw', spaceHex(record.hex)))
  return card
}

/** Builds a card for a decoded Answer-To-Reset. */
function buildAtrCard(atr: AtrInfo, rawHex: string, time: string): HTMLElement {
  const card = el('article', 'card card--atr')
  card.append(cardHead('ATR', 'badge--atr', 'Answer To Reset', '', time))

  if (atr.ok) {
    const chips = el('div', 'chips')
    chips.append(
      chip('Convention', atr.convention),
      chip('Protocols', atr.protocols),
      chip('Historical', String(atr.historicalCount))
    )
    card.append(chips)
    if (atr.historical) card.append(dataRow('Historical', atr.historical))
    for (const note of atr.historicalNotes) card.append(el('p', 'card-note', note))
  } else {
    card.append(el('p', 'card-note', atr.message))
  }

  card.append(dataRow('ATR', rawHex))
  return card
}

/** Adds a Copy button to a card's head that puts the raw APDU hex on the clipboard. */
function attachCopy(card: HTMLElement, hex: string): void {
  const head = card.querySelector('.card-head')
  if (!head) return

  const button = el('button', 'card-copy', 'Copy')
  button.type = 'button'
  button.title = 'Copy APDU hex'
  button.addEventListener('click', () => {
    navigator.clipboard.writeText(hex).then(
      () => {
        button.textContent = 'Copied'
        button.classList.add('copied')
        setTimeout(() => {
          button.textContent = 'Copy'
          button.classList.remove('copied')
        }, 1200)
      },
      () => setStatus('error', 'Could not copy to clipboard')
    )
  })
  head.append(button)
}

// --- Filtering --------------------------------------------------------------

function filterActive(): boolean {
  return filter.query !== '' || filter.direction !== 'all' || filter.issuesOnly
}

function entryVisible(entry: RenderedEntry): boolean {
  if (filter.direction !== 'all' && entry.direction !== filter.direction) return false
  if (filter.issuesOnly && entry.status !== 'warning' && entry.status !== 'error') return false
  if (filter.query && !entry.search.includes(filter.query)) return false
  return true
}

/** The exportable APDUs currently passing the filter, in capture order. */
function visibleTrace(): TraceEntry[] {
  const out: TraceEntry[] = []
  for (const entry of entries) {
    if (entry.record && entryVisible(entry)) out.push(entry.record)
  }
  return out
}

/** Save is enabled only when at least one APDU would be exported. */
function updateSaveState(): void {
  saveButton.disabled = visibleTrace().length === 0
}

/** Scrolls the detailed view to the next error/warning card, wrapping around. */
function jumpToNextIssue(): void {
  const isIssue = (entry: RenderedEntry): boolean =>
    (entry.status === 'error' || entry.status === 'warning') && entryVisible(entry)
  if (!entries.some(isIssue)) {
    setStatus('info', 'No issues in the current view.')
    return
  }

  let target = -1
  for (let step = 1; step <= entries.length; step += 1) {
    const index = (lastIssueIndex + step) % entries.length
    if (isIssue(entries[index])) {
      target = index
      break
    }
  }
  if (target === -1) return

  lastIssueIndex = target
  if (activeView !== 'detailed') selectView('detailed')
  const card = entries[target].card
  card.scrollIntoView({ block: 'center', behavior: 'smooth' })
  card.classList.add('card--flash')
  setTimeout(() => card.classList.remove('card--flash'), 1000)
}

/** Re-applies the filter to every entry: rebuilds the log and toggles cards. */
function applyFilter(): void {
  const lines: string[] = []
  for (const entry of entries) {
    const visible = entryVisible(entry)
    entry.card.hidden = !visible
    if (visible) lines.push(entry.line)
  }
  output.value = lines.join('\n')

  if (filterActive() && entries.length > 0) {
    filterCount.hidden = false
    filterCount.textContent = `${lines.length} of ${entries.length} shown`
  } else {
    filterCount.hidden = true
  }
  updateSaveState()
  if (autoScroll) scrollToBottom()
}

/** Adds an entry to both views, respecting the active filter. */
function pushEntry(entry: RenderedEntry): void {
  entries.push(entry)
  cards.append(entry.card)
  clearButton.disabled = false

  if (filterActive()) {
    applyFilter()
    return
  }
  entry.card.hidden = false
  output.value += (output.value ? '\n' : '') + entry.line
  if (autoScroll) scrollToBottom()
}

function pushMeta(line: string, cardText: string, isError = false): void {
  const card = el('div', isError ? 'meta meta--error' : 'meta', cardText)
  pushEntry({
    line,
    card,
    search: `${line} ${cardText}`.toLowerCase(),
    direction: null,
    status: isError ? 'error' : null
  })
}

// --- TLV profile ------------------------------------------------------------

/** The profile used for TLV decoding: the manual choice, or the auto-detected one. */
function activeProfile(): TlvProfile {
  return profileChoice === 'auto' ? detectedProfile : profileChoice
}

/** Updates the auto-detected profile from a SELECT-by-AID command. */
function detectProfileFromSelect(command: CommandApdu): void {
  if (command.ins !== 0xa4 || command.p1 !== 0x04 || !command.data) return
  const detected = detectProfile(command.data)
  if (detected && detected !== detectedProfile) {
    detectedProfile = detected
    if (profileChoice === 'auto') setStatus('info', `Detected ${PROFILE_LABELS[detected]} application`)
  }
}

/** Re-decodes every response card after the profile selection changes. */
function applyProfile(): void {
  profileChoice = profileSelect.value as 'auto' | TlvProfile
  for (const entry of entries) {
    if (!entry.rebuild) continue
    const profile = profileChoice === 'auto' ? entry.captureProfile ?? 'iso' : profileChoice
    const fresh = entry.rebuild(profile)
    fresh.hidden = !entryVisible(entry)
    entry.card.replaceWith(fresh)
    entry.card = fresh
  }
  if (activeView === 'decode') renderDecode()
}

/** `arrivedAt` is null for imported APDUs, where wall-clock latency is unknown. */
function pushApdu(record: ApduRecord, time: string, arrivedAt: number | null): void {
  const delta = arrivedAt !== null && lastArrivedAt !== null ? arrivedAt - lastArrivedAt : null
  if (arrivedAt !== null) lastArrivedAt = arrivedAt
  const stamp = delta !== null ? `${time} ${formatDelta(delta)}` : time
  const line = `[${stamp}] ${record.direction === 'command' ? '>>' : '<<'} ${spaceHex(record.hex)}`
  const hexText = `${record.hex} ${spaceHex(record.hex)}`
  let card: HTMLElement
  let search: string
  let status: StatusKind | null = null
  let rebuild: ((profile: TlvProfile) => HTMLElement) | undefined
  let captureProfile: TlvProfile | undefined

  if (record.direction === 'command') {
    const parsed = parseCommand(record.hex)
    if (parsed.kind === 'malformed') {
      card = buildMalformedCard(record, parsed, time)
      search = `${hexText} ${parsed.message} malformed`
      status = 'error'
    } else {
      card = buildCommandCard(parsed, time)
      search = `${hexText} ${parsed.name} ${parsed.note} case ${parsed.apduCase}`
      if (parsed.secureMessaging) search += ` secure messaging ${parsed.secureMessaging}`
      detectProfileFromSelect(parsed)
    }
    attachCopy(card, record.hex)
    pendingByDevice.set(record.device, {
      ins: parsed.kind === 'command' ? parsed.ins : null,
      name: parsed.kind === 'command' ? parsed.name : 'Malformed command',
      startedAt: arrivedAt
    })
  } else {
    const parsed = parseResponse(record.hex)
    const pending = pendingByDevice.get(record.device) ?? null
    const pairing: ResponsePairing | null =
      pending && pending.startedAt !== null && arrivedAt !== null
        ? { latencyMs: arrivedAt - pending.startedAt, commandIns: pending.ins }
        : null
    if (pairing && pending) latencies.push({ name: pending.name, ms: pairing.latencyMs })
    if (parsed.kind === 'malformed') {
      card = buildMalformedCard(record, parsed, time)
      search = `${hexText} ${parsed.message} malformed`
      status = 'error'
      attachCopy(card, record.hex)
    } else {
      const response = parsed
      captureProfile = activeProfile()
      rebuild = (profile: TlvProfile): HTMLElement => {
        const built = buildResponseCard(response, time, pairing, profile)
        attachCopy(built, record.hex)
        return built
      }
      card = rebuild(captureProfile)
      search = `${hexText} ${parsed.meaning}`
      status = parsed.status
    }
    pendingByDevice.delete(record.device)
  }

  pushEntry({
    line,
    card,
    search: search.toLowerCase(),
    direction: record.direction,
    status,
    record: { time, direction: record.direction, hex: record.hex },
    rebuild,
    captureProfile
  })
}

/** Adds a decoded ATR to the trace views. ATRs are shown but not exported. */
function pushAtr(hex: string, time: string): void {
  const rawHex = spaceHex(hex)
  const atr = parseAtr(hex)
  const card = buildAtrCard(atr, rawHex, time)
  attachCopy(card, hex)
  pushEntry({
    line: `[${time}] ATR ${rawHex}`,
    card,
    search: `atr ${hex} ${rawHex} ${atr.convention} ${atr.protocols}`.toLowerCase(),
    direction: null,
    status: atr.ok ? null : 'error'
  })
}

// --- Shared UI state --------------------------------------------------------

function setStatus(kind: StatusMessage['kind'], text: string): void {
  statusElement.textContent = text
  statusElement.dataset.kind = kind
}

function updateCounter(): void {
  counterElement.textContent = `Cmd ${commandCount} · Rsp ${responseCount}`
}

// --- Summary view -----------------------------------------------------------

function summarySection(title: string): HTMLElement {
  const section = el('section', 'summary-section')
  section.append(el('h2', 'summary-title', title))
  return section
}

function statRow(label: string, value: string): HTMLElement {
  const row = el('div', 'summary-row')
  row.append(el('span', 'summary-label', label), el('span', 'summary-value', value))
  return row
}

/** Recomputes the whole-session summary from the captured trace. */
function renderSummary(): void {
  summary.replaceChildren()
  if (trace.length === 0) {
    summary.append(el('p', 'summary-empty', 'No APDUs captured yet.'))
    return
  }

  let commands = 0
  let responses = 0
  let malformed = 0
  let success = 0
  let warning = 0
  let errors = 0
  const byInstruction = new Map<string, number>()

  for (const entry of trace) {
    if (entry.direction === 'command') {
      commands += 1
      const parsed = parseCommand(entry.hex)
      if (parsed.kind === 'malformed') malformed += 1
      else byInstruction.set(parsed.name, (byInstruction.get(parsed.name) ?? 0) + 1)
    } else {
      responses += 1
      const parsed = parseResponse(entry.hex)
      if (parsed.kind === 'malformed') malformed += 1
      else if (parsed.status === 'success') success += 1
      else if (parsed.status === 'warning') warning += 1
      else errors += 1
    }
  }

  const overview = summarySection('Overview')
  overview.append(
    statRow('Total APDUs', String(trace.length)),
    statRow('Commands', String(commands)),
    statRow('Responses', String(responses)),
    statRow('Malformed', String(malformed))
  )
  summary.append(overview)

  const decoded = success + warning + errors
  const status = summarySection('Responses by status')
  status.append(
    statRow('Success', String(success)),
    statRow('Warning', String(warning)),
    statRow('Error', String(errors)),
    statRow('Error rate', decoded > 0 ? `${((errors / decoded) * 100).toFixed(1)}%` : '—')
  )
  summary.append(status)

  const latency = summarySection('Latency')
  if (latencies.length === 0) {
    latency.append(
      el('p', 'summary-empty', 'No latency data — imported and unpaired responses are excluded.')
    )
  } else {
    const values = latencies.map((sample) => sample.ms)
    const average = values.reduce((sum, ms) => sum + ms, 0) / values.length
    const slowest = latencies.reduce((worst, sample) => (sample.ms > worst.ms ? sample : worst))
    latency.append(
      statRow('Paired responses', String(latencies.length)),
      statRow('Minimum', `${Math.min(...values)} ms`),
      statRow('Average', `${average.toFixed(1)} ms`),
      statRow('Maximum', `${Math.max(...values)} ms`),
      statRow('Slowest', `${slowest.name} — ${slowest.ms} ms`)
    )
  }
  summary.append(latency)

  const instructions = summarySection('Commands by instruction')
  if (byInstruction.size === 0) {
    instructions.append(el('p', 'summary-empty', 'No decoded commands.'))
  } else {
    for (const [name, count] of [...byInstruction].sort((a, b) => b[1] - a[1])) {
      instructions.append(statRow(name, String(count)))
    }
  }
  summary.append(instructions)
}

/** Re-renders the summary only when it is the visible view. */
function refreshSummary(): void {
  if (activeView === 'summary') renderSummary()
}

// --- Decode view ------------------------------------------------------------

/** Decodes the hex typed into the Decode tab and shows it as a card. */
function renderDecode(): void {
  decodeResult.replaceChildren()
  const hex = decodeInput.value.replace(/[^0-9a-fA-F]/g, '').toUpperCase()
  if (hex.length === 0) {
    decodeResult.append(el('p', 'summary-empty', 'Paste APDU hex above to decode it.'))
    return
  }

  const direction: 'command' | 'response' = decodeCommandRadio.checked ? 'command' : 'response'
  const record: ApduRecord = { direction, hex, device: '' }
  let card: HTMLElement

  if (direction === 'command') {
    const parsed = parseCommand(hex)
    card = parsed.kind === 'malformed' ? buildMalformedCard(record, parsed, '') : buildCommandCard(parsed, '')
  } else {
    const parsed = parseResponse(hex)
    card =
      parsed.kind === 'malformed'
        ? buildMalformedCard(record, parsed, '')
        : buildResponseCard(parsed, '', null, activeProfile())
  }

  attachCopy(card, hex)
  decodeResult.append(card)
}

function clearTrace(): void {
  trace.length = 0
  entries.length = 0
  latencies.length = 0
  commandCount = 0
  responseCount = 0
  pendingByDevice.clear()
  detectedProfile = 'iso'
  lastArrivedAt = null
  lastIssueIndex = -1
  output.value = ''
  cards.replaceChildren()
  filterCount.hidden = true
  updateCounter()
  refreshSummary()
  updateSaveState()
  clearButton.disabled = true
  setStatus('info', 'Cleared.')
}

function applyTheme(theme: ThemePreference): void {
  if (theme === 'system') delete document.documentElement.dataset.theme
  else document.documentElement.dataset.theme = theme
}

function selectView(name: ViewPreference, persist = true): void {
  activeView = name
  for (const key of VIEW_ORDER) {
    const active = key === name
    views[key].tab.setAttribute('aria-selected', String(active))
    views[key].panel.hidden = !active
  }
  if (name === 'summary') renderSummary()
  else if (name === 'decode') renderDecode()
  if (autoScroll) scrollToBottom()
  if (persist) void window.api.updateSettings({ view: name })
}

function setTracing(on: boolean): void {
  tracing = on
  toggleButton.textContent = on ? 'Stop tracing' : 'Start tracing'
  toggleButton.classList.toggle('tracing', on)
  interfaceSelect.disabled = on
  refreshButton.disabled = on
  openFileButton.disabled = on
  importButton.disabled = on
}

/** Opens a .pcap/.pcapng capture file and replays its APDUs. */
async function openCapture(): Promise<void> {
  openFileButton.disabled = true
  try {
    const result = await window.api.openCaptureFile()
    if (!result.started) {
      if (result.error) setStatus('error', `Open failed: ${result.error}`)
      return
    }
    clearTrace()
    pushMeta(`--- ${timestamp()}  opened capture ${result.path} ---`, `Opened capture ${result.path}`)
    setTracing(true)
    setStatus('info', `Reading capture from ${result.path}…`)
  } finally {
    openFileButton.disabled = tracing
  }
}

/** Replaces the current trace with the APDUs from a JSON file. */
async function importTrace(): Promise<void> {
  importButton.disabled = true
  try {
    const result = await window.api.importTrace()
    if (!result.imported || !result.apdus) {
      if (result.error) setStatus('error', `Import failed: ${result.error}`)
      return
    }
    clearTrace()
    pushMeta(
      `--- ${timestamp()}  imported ${result.apdus.length} APDU(s) ---`,
      `Imported ${result.apdus.length} APDU(s) from ${result.path}`
    )
    for (const entry of result.apdus) {
      pushApdu({ direction: entry.direction, hex: entry.hex, device: '' }, entry.time, null)
      trace.push(entry)
      if (entry.direction === 'command') commandCount += 1
      else responseCount += 1
    }
    updateCounter()
    refreshSummary()
    updateSaveState()
    setStatus('info', `Imported ${result.apdus.length} APDU(s) from ${result.path}`)
  } finally {
    importButton.disabled = tracing
  }
}

async function refreshInterfaces(): Promise<void> {
  interfaceSelect.replaceChildren()
  try {
    const items = await window.api.listInterfaces()
    tsharkButton.hidden = true
    if (items.length === 0) {
      const empty = new Option('No USBPcap interfaces found', '')
      empty.disabled = true
      interfaceSelect.add(empty)
      toggleButton.disabled = true
      setStatus('error', 'No USBPcap interfaces — is USBPcap installed?')
      return
    }
    for (const item of items) {
      interfaceSelect.add(new Option(item.description, item.name))
    }
    toggleButton.disabled = false
    setStatus('info', `Found ${items.length} USBPcap interface(s).`)
  } catch (err) {
    tsharkButton.hidden = false
    toggleButton.disabled = true
    setStatus('error', `Could not list interfaces: ${(err as Error).message}`)
  }
}

/** Lets the user point the app at a specific tshark.exe, then re-lists interfaces. */
async function chooseTshark(): Promise<void> {
  const path = await window.api.locateTshark()
  if (!path) return
  setStatus('info', `Using tshark at ${path}`)
  await refreshInterfaces()
}

async function loadSettings(): Promise<void> {
  const settings = await window.api.getSettings()
  autoScroll = settings.autoScroll
  autoScrollInput.checked = settings.autoScroll
  themeSelect.value = settings.theme
  applyTheme(settings.theme)
  selectView(settings.view, false)
}

// --- Events -----------------------------------------------------------------

themeSelect.addEventListener('change', () => {
  const theme = themeSelect.value as ThemePreference
  applyTheme(theme)
  void window.api.updateSettings({ theme })
})

autoScrollInput.addEventListener('change', () => {
  autoScroll = autoScrollInput.checked
  if (autoScroll) scrollToBottom()
  void window.api.updateSettings({ autoScroll })
})

for (const key of VIEW_ORDER) {
  views[key].tab.addEventListener('click', () => selectView(key))
}

tabList.addEventListener('keydown', (event) => {
  if (event.key !== 'ArrowLeft' && event.key !== 'ArrowRight') return
  const delta = event.key === 'ArrowRight' ? 1 : -1
  const next = (VIEW_ORDER.indexOf(activeView) + delta + VIEW_ORDER.length) % VIEW_ORDER.length
  selectView(VIEW_ORDER[next])
  views[activeView].tab.focus()
})

refreshButton.addEventListener('click', () => {
  void refreshInterfaces()
})

tsharkButton.addEventListener('click', () => {
  void chooseTshark()
})

searchInput.addEventListener('input', () => {
  filter.query = searchInput.value.trim().toLowerCase()
  applyFilter()
})

directionSelect.addEventListener('change', () => {
  filter.direction = directionSelect.value as DirectionFilter
  applyFilter()
})

issuesInput.addEventListener('change', () => {
  filter.issuesOnly = issuesInput.checked
  applyFilter()
})

profileSelect.addEventListener('change', () => applyProfile())

decodeInput.addEventListener('input', () => renderDecode())
decodeKindGroup.addEventListener('change', () => renderDecode())

document.addEventListener('keydown', (event) => {
  if ((event.ctrlKey || event.metaKey) && event.key === 'f') {
    event.preventDefault()
    searchInput.focus()
    searchInput.select()
    return
  }
  if (event.key === 'F3') {
    event.preventDefault()
    jumpToNextIssue()
  }
})

toggleButton.addEventListener('click', () => {
  if (tracing) {
    window.api.stopTrace()
    return
  }
  const iface = interfaceSelect.value
  if (!iface) return
  pushMeta(`--- ${timestamp()}  started tracing on ${iface} ---`, `Started tracing on ${iface}`)
  window.api.startTrace(iface)
  setTracing(true)
})

saveButton.addEventListener('click', async () => {
  const visible = visibleTrace()
  if (visible.length === 0) return
  saveButton.disabled = true
  try {
    const result = await window.api.exportTrace(visible)
    if (result.saved) {
      const scope = filterActive() ? 'filtered ' : ''
      setStatus('info', `Saved ${visible.length} ${scope}APDU(s) to ${result.path}`)
    } else if (result.error) {
      setStatus('error', `Save failed: ${result.error}`)
    }
  } finally {
    updateSaveState()
  }
})

importButton.addEventListener('click', () => {
  void importTrace()
})

openFileButton.addEventListener('click', () => {
  void openCapture()
})

clearButton.addEventListener('click', () => clearTrace())

window.api.onApdu((record: ApduRecord) => {
  const time = timestamp()
  pushApdu(record, time, Date.now())
  trace.push({ time, direction: record.direction, hex: record.hex })
  if (record.direction === 'command') commandCount += 1
  else responseCount += 1
  updateCounter()
  refreshSummary()
  updateSaveState()
})

window.api.onAtr((hex: string) => {
  pushAtr(hex, timestamp())
})

window.api.onStatus((message: StatusMessage) => {
  setStatus(message.kind, message.text)
  if (message.kind === 'error') {
    pushMeta(`[${timestamp()}]  ! ${message.text}`, message.text, true)
  }
})

window.api.onStopped(() => {
  if (tracing) {
    pushMeta(`--- ${timestamp()}  stopped ---`, 'Tracing stopped')
  }
  setTracing(false)
})

void loadSettings()
void refreshInterfaces()
