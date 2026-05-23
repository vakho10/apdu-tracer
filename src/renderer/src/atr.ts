// ISO 7816-3 Answer-To-Reset (ATR) parsing.

import { byteHex, toBytes } from './apdu'

export interface AtrInfo {
  ok: boolean
  message: string // populated when ok is false
  convention: string // 'Direct' or 'Inverse'
  protocols: string // offered transmission protocols, e.g. 'T=0, T=1'
  historical: string // spaced uppercase hex of the historical bytes, '' when none
  historicalCount: number
  historicalNotes: string[] // decoded historical-byte interpretation lines
}

function spacedHex(bytes: number[]): string {
  return bytes.map(byteHex).join(' ')
}

// COMPACT-TLV interindustry tags carried in the historical bytes (the high
// nibble of each object's lead byte; the full tag is 0x40 | nibble).
const COMPACT_TLV_TAGS: Record<number, string> = {
  0x1: 'Country code',
  0x2: 'Issuer identification number',
  0x3: 'Card service data',
  0x4: 'Initial access data',
  0x5: 'Card issuer data',
  0x6: 'Pre-issuing data',
  0x7: 'Card capabilities',
  0x8: 'Status indicator',
  0xf: 'Application identifier'
}

/** Decodes the COMPACT-TLV data objects packed into the historical bytes. */
function decodeCompactTlv(bytes: number[]): string[] {
  const out: string[] = []
  let pos = 0
  while (pos < bytes.length) {
    const tag = bytes[pos] >> 4
    const len = bytes[pos] & 0x0f
    pos += 1
    if (pos + len > bytes.length) break // truncated object
    const name = COMPACT_TLV_TAGS[tag] ?? `Tag ${tag.toString(16).toUpperCase()}`
    out.push(`${name}: ${spacedHex(bytes.slice(pos, pos + len))}`)
    pos += len
  }
  return out
}

/** Interprets the ISO 7816-4 historical bytes from their category indicator. */
function decodeHistorical(bytes: number[]): string[] {
  if (bytes.length === 0) return []
  const category = bytes[0]

  if (category === 0x00) {
    const out = ['Category: status at end of historical bytes']
    out.push(...decodeCompactTlv(bytes.length > 4 ? bytes.slice(1, bytes.length - 3) : []))
    if (bytes.length >= 4) {
      const s = bytes.slice(bytes.length - 3)
      out.push(`Status: life cycle ${byteHex(s[0])}, SW ${byteHex(s[1])}${byteHex(s[2])}`)
    }
    return out
  }
  if (category === 0x80) {
    return ['Category: COMPACT-TLV data objects', ...decodeCompactTlv(bytes.slice(1))]
  }
  if (category === 0x10) {
    return ['Category: DIR data reference']
  }
  return [`Category: proprietary (0x${byteHex(category)})`]
}

/** Parses an ATR into its convention, offered protocols and historical bytes. */
export function parseAtr(hex: string): AtrInfo {
  const bytes = toBytes(hex)
  const fail = (message: string): AtrInfo => ({
    ok: false,
    message,
    convention: '',
    protocols: '',
    historical: '',
    historicalCount: 0,
    historicalNotes: []
  })

  if (bytes.length < 2) return fail('ATR too short — need at least TS and T0')

  const ts = bytes[0]
  let convention: string
  if (ts === 0x3b) convention = 'Direct'
  else if (ts === 0x3f) convention = 'Inverse'
  else return fail(`Invalid initial character TS = 0x${byteHex(ts)}`)

  const t0 = bytes[1]
  const historicalCount = t0 & 0x0f
  let y = t0 >> 4
  let pos = 2
  const protocols = new Set<number>()

  // Walk the interface-byte groups; each TD byte announces the next group.
  while (true) {
    if (y & 0x01) pos += 1 // TA
    if (y & 0x02) pos += 1 // TB
    if (y & 0x04) pos += 1 // TC
    if (pos > bytes.length) return fail('ATR truncated within interface bytes')
    if (y & 0x08) {
      if (pos >= bytes.length) return fail('ATR truncated within interface bytes')
      const td = bytes[pos]
      pos += 1
      protocols.add(td & 0x0f)
      y = td >> 4
    } else {
      break
    }
  }

  if (pos + historicalCount > bytes.length) {
    return fail('ATR truncated within historical bytes')
  }
  const historical = bytes.slice(pos, pos + historicalCount)

  // T=15 is a global-parameters marker, not a transmission protocol.
  const transmission = [...protocols].filter((p) => p !== 15).sort((a, b) => a - b)
  if (transmission.length === 0) transmission.push(0)

  return {
    ok: true,
    message: '',
    convention,
    protocols: transmission.map((p) => `T=${p}`).join(', '),
    historical: spacedHex(historical),
    historicalCount,
    historicalNotes: decodeHistorical(historical)
  }
}
