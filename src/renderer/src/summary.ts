import { el } from './dom'

/** A measured command/response round-trip, used by the latency breakdown. */
export interface LatencySample {
  name: string
  ms: number
}

/**
 * Running summary aggregates, updated as each APDU arrives so the summary view
 * never has to re-parse the whole trace.
 */
export interface SummaryStats {
  malformed: number
  success: number
  warning: number
  errors: number
  byInstruction: Map<string, number>
}

/** Everything the summary view needs, gathered from the renderer's live state. */
export interface SummaryData {
  total: number
  commands: number
  responses: number
  stats: SummaryStats
  latencies: LatencySample[]
}

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

/** Renders the whole-session summary from the running aggregates into `target`. */
export function renderSummary(target: HTMLElement, data: SummaryData): void {
  target.replaceChildren()
  if (data.total === 0) {
    target.append(el('p', 'summary-empty', 'No APDUs captured yet.'))
    return
  }

  const { malformed, success, warning, errors, byInstruction } = data.stats

  const overview = summarySection('Overview')
  overview.append(
    statRow('Total APDUs', String(data.total)),
    statRow('Commands', String(data.commands)),
    statRow('Responses', String(data.responses)),
    statRow('Malformed', String(malformed))
  )
  target.append(overview)

  const decoded = success + warning + errors
  const status = summarySection('Responses by status')
  status.append(
    statRow('Success', String(success)),
    statRow('Warning', String(warning)),
    statRow('Error', String(errors)),
    statRow('Error rate', decoded > 0 ? `${((errors / decoded) * 100).toFixed(1)}%` : '—')
  )
  target.append(status)

  const latency = summarySection('Latency')
  if (data.latencies.length === 0) {
    latency.append(
      el('p', 'summary-empty', 'No latency data — imported and unpaired responses are excluded.')
    )
  } else {
    const values = data.latencies.map((sample) => sample.ms)
    const average = values.reduce((sum, ms) => sum + ms, 0) / values.length
    const slowest = data.latencies.reduce((worst, sample) =>
      sample.ms > worst.ms ? sample : worst
    )
    latency.append(
      statRow('Paired responses', String(data.latencies.length)),
      statRow('Minimum', `${Math.min(...values)} ms`),
      statRow('Average', `${average.toFixed(1)} ms`),
      statRow('Maximum', `${Math.max(...values)} ms`),
      statRow('Slowest', `${slowest.name} — ${slowest.ms} ms`)
    )
  }
  target.append(latency)

  const instructions = summarySection('Commands by instruction')
  if (byInstruction.size === 0) {
    instructions.append(el('p', 'summary-empty', 'No decoded commands.'))
  } else {
    for (const [name, count] of [...byInstruction].sort((a, b) => b[1] - a[1])) {
      instructions.append(statRow(name, String(count)))
    }
  }
  target.append(instructions)
}
