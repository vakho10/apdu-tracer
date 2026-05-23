// BER-TLV parsing for the data field of response APDUs (ISO 7816-4).
// Tag meanings are resolved against an application profile (EMV, PIV,
// OpenPGP) layered over the generic ISO 7816-4 interindustry set.

import { byteHex, toBytes } from './apdu'

export type TlvProfile = 'iso' | 'emv' | 'piv' | 'openpgp'

export interface TlvNode {
  tag: string // concatenated uppercase hex of all tag bytes
  name: string // common meaning, '' when the tag is unknown
  constructed: boolean
  length: number
  value: string // spaced uppercase hex of the raw value bytes
  decoded: string[] // human-readable interpretation lines, [] when none
  children: TlvNode[]
}

export interface TlvResult {
  ok: boolean
  nodes: TlvNode[]
}

// Common ISO 7816-4 interindustry data-object tags. Meanings can depend on the
// application context; these are the usual interpretations and act as the
// fallback when a profile does not define a tag.
const ISO_TAGS: Record<string, string> = {
  '6F': 'FCI template',
  '62': 'FCP template',
  '64': 'FMD template',
  '61': 'Application template',
  '6E': 'Application-related data',
  '4F': 'Application identifier (AID)',
  '50': 'Application label',
  '51': 'File reference / path',
  '52': 'Command APDU',
  '53': 'Discretionary data',
  '73': 'Discretionary data objects',
  '5F2D': 'Language preference',
  '5F50': 'Issuer URL',
  '84': 'DF name',
  '85': 'Proprietary information',
  A5: 'FCI proprietary template',
  '87': 'EF holding an FCI extension',
  '88': 'Short EF identifier (SFI)',
  '8A': 'Life-cycle status byte',
  '82': 'File descriptor',
  '83': 'File identifier',
  '80': 'File size (excluding structural information)',
  '81': 'Total file size (including structural information)',
  '86': 'Security attributes (proprietary)',
  '8B': 'Security attributes (referencing expanded format)',
  '8C': 'Security attributes (compact format)',
  '8D': 'Security attributes (expanded format)',
  '70': 'Record / READ RECORD response template',
  '77': 'Response message template',
  C6: 'PIN status template'
}

// EMV (Book 3) payment-card data objects.
const EMV_TAGS: Record<string, string> = {
  '4F': 'Application Identifier (AID)',
  '50': 'Application Label',
  '56': 'Track 1 Data',
  '57': 'Track 2 Equivalent Data',
  '5A': 'Application Primary Account Number (PAN)',
  '5F20': 'Cardholder Name',
  '5F24': 'Application Expiration Date',
  '5F25': 'Application Effective Date',
  '5F28': 'Issuer Country Code',
  '5F2A': 'Transaction Currency Code',
  '5F2D': 'Language Preference',
  '5F30': 'Service Code',
  '5F34': 'Application PAN Sequence Number',
  '5F36': 'Transaction Currency Exponent',
  '61': 'Application Template',
  '6F': 'File Control Information (FCI) Template',
  '70': 'READ RECORD Response Message Template',
  '71': 'Issuer Script Template 1',
  '72': 'Issuer Script Template 2',
  '73': 'Directory Discretionary Template',
  '77': 'Response Message Template Format 2',
  '80': 'Response Message Template Format 1',
  '82': 'Application Interchange Profile',
  '83': 'Command Template',
  '84': 'Dedicated File (DF) Name',
  '86': 'Issuer Script Command',
  '87': 'Application Priority Indicator',
  '88': 'Short File Identifier (SFI)',
  '89': 'Authorisation Code',
  '8A': 'Authorisation Response Code',
  '8C': 'Card Risk Management Data Object List 1 (CDOL1)',
  '8D': 'Card Risk Management Data Object List 2 (CDOL2)',
  '8E': 'Cardholder Verification Method (CVM) List',
  '8F': 'Certification Authority Public Key Index',
  '90': 'Issuer Public Key Certificate',
  '91': 'Issuer Authentication Data',
  '92': 'Issuer Public Key Remainder',
  '93': 'Signed Static Application Data',
  '94': 'Application File Locator (AFL)',
  '95': 'Terminal Verification Results',
  '97': 'Transaction Certificate Data Object List (TDOL)',
  '98': 'Transaction Certificate (TC) Hash Value',
  '99': 'Transaction PIN Data',
  '9A': 'Transaction Date',
  '9B': 'Transaction Status Information',
  '9C': 'Transaction Type',
  '9D': 'Directory Definition File (DDF) Name',
  A5: 'FCI Proprietary Template',
  '9F01': 'Acquirer Identifier',
  '9F02': 'Amount, Authorised (Numeric)',
  '9F03': 'Amount, Other (Numeric)',
  '9F04': 'Amount, Other (Binary)',
  '9F05': 'Application Discretionary Data',
  '9F06': 'Application Identifier (AID) — terminal',
  '9F07': 'Application Usage Control',
  '9F08': 'Application Version Number — card',
  '9F09': 'Application Version Number — terminal',
  '9F0B': 'Cardholder Name Extended',
  '9F0D': 'Issuer Action Code — Default',
  '9F0E': 'Issuer Action Code — Denial',
  '9F0F': 'Issuer Action Code — Online',
  '9F10': 'Issuer Application Data',
  '9F11': 'Issuer Code Table Index',
  '9F12': 'Application Preferred Name',
  '9F13': 'Last Online ATC Register',
  '9F14': 'Lower Consecutive Offline Limit',
  '9F15': 'Merchant Category Code',
  '9F16': 'Merchant Identifier',
  '9F17': 'PIN Try Counter',
  '9F18': 'Issuer Script Identifier',
  '9F1A': 'Terminal Country Code',
  '9F1B': 'Terminal Floor Limit',
  '9F1C': 'Terminal Identification',
  '9F1D': 'Terminal Risk Management Data',
  '9F1E': 'Interface Device (IFD) Serial Number',
  '9F1F': 'Track 1 Discretionary Data',
  '9F20': 'Track 2 Discretionary Data',
  '9F21': 'Transaction Time',
  '9F22': 'Certification Authority Public Key Index — terminal',
  '9F23': 'Upper Consecutive Offline Limit',
  '9F26': 'Application Cryptogram',
  '9F27': 'Cryptogram Information Data',
  '9F32': 'Issuer Public Key Exponent',
  '9F33': 'Terminal Capabilities',
  '9F34': 'Cardholder Verification Method (CVM) Results',
  '9F35': 'Terminal Type',
  '9F36': 'Application Transaction Counter (ATC)',
  '9F37': 'Unpredictable Number',
  '9F38': 'Processing Options Data Object List (PDOL)',
  '9F39': 'Point-of-Service (POS) Entry Mode',
  '9F3A': 'Amount, Reference Currency',
  '9F3B': 'Application Reference Currency',
  '9F3C': 'Transaction Reference Currency Code',
  '9F3D': 'Transaction Reference Currency Exponent',
  '9F40': 'Additional Terminal Capabilities',
  '9F41': 'Transaction Sequence Counter',
  '9F42': 'Application Currency Code',
  '9F43': 'Application Reference Currency Exponent',
  '9F44': 'Application Currency Exponent',
  '9F45': 'Data Authentication Code',
  '9F46': 'ICC Public Key Certificate',
  '9F47': 'ICC Public Key Exponent',
  '9F48': 'ICC Public Key Remainder',
  '9F49': 'Dynamic Data Authentication Data Object List (DDOL)',
  '9F4A': 'Static Data Authentication Tag List',
  '9F4B': 'Signed Dynamic Application Data',
  '9F4C': 'ICC Dynamic Number',
  '9F4D': 'Log Entry',
  '9F4E': 'Merchant Name and Location',
  '9F4F': 'Log Format',
  '9F50': 'Offline Accumulator Balance',
  '9F5A': 'Application Program Identifier',
  '9F66': 'Terminal Transaction Qualifiers (TTQ)',
  '9F6B': 'Track 2 Data',
  '9F6C': 'Card Transaction Qualifiers (CTQ)',
  '9F6E': 'Form Factor Indicator',
  BF0C: 'FCI Issuer Discretionary Data'
}

// NIST SP 800-73 PIV (Personal Identity Verification) data objects.
const PIV_TAGS: Record<string, string> = {
  '4F': 'Application Identifier (AID)',
  '50': 'Application Label',
  '53': 'Discretionary data (PIV data object)',
  '5C': 'Tag list',
  '5F2F': 'PIN usage policy',
  '5F50': 'Uniform Resource Locator',
  '79': 'Coexistent tag allocation authority',
  '7E': 'Discovery Object',
  '06': 'Object identifier (OID)',
  '30': 'FASC-N',
  '32': 'Organizational identifier',
  '33': 'DUNS',
  '34': 'GUID / Cardholder UUID',
  '35': 'Expiration date',
  '3D': 'Authentication key map',
  '3E': 'Issuer asymmetric signature',
  '70': 'Certificate',
  '71': 'CertInfo',
  '72': 'MSCUID',
  '80': 'Cryptographic mechanism identifier',
  '81': 'Parameter',
  AC: 'Cryptographic algorithm identifier template',
  EE: 'Buffer length',
  FE: 'Error detection code'
}

// OpenPGP card application data objects.
const OPENPGP_TAGS: Record<string, string> = {
  '4F': 'Application identifier (AID)',
  '5B': 'Name',
  '5E': 'Login data',
  '5F2D': 'Language preference',
  '5F35': 'Sex',
  '5F50': 'URL',
  '5F52': 'Historical bytes',
  '65': 'Cardholder Related Data',
  '6E': 'Application Related Data',
  '73': 'Discretionary data objects',
  '7A': 'Security support template',
  '7F21': 'Cardholder certificate',
  '7F48': 'Cardholder private key template',
  '7F49': 'Public key template',
  '7F66': 'Extended length information',
  '7F74': 'General feature management',
  '5F48': 'Cardholder private key',
  '81': 'RSA modulus',
  '82': 'RSA public exponent',
  '93': 'Digital signature counter',
  C0: 'Extended capabilities',
  C1: 'Algorithm attributes: signature key',
  C2: 'Algorithm attributes: decryption key',
  C3: 'Algorithm attributes: authentication key',
  C4: 'PW Status Bytes',
  C5: 'Fingerprints',
  C6: 'CA fingerprints',
  C7: 'Fingerprint: signature key',
  C8: 'Fingerprint: decryption key',
  C9: 'Fingerprint: authentication key',
  CD: 'Key generation dates/times',
  D0: 'Resetting code',
  D3: 'Resetting code'
}

const PROFILE_TAGS: Record<TlvProfile, Record<string, string>> = {
  iso: {},
  emv: EMV_TAGS,
  piv: PIV_TAGS,
  openpgp: OPENPGP_TAGS
}

/** Resolves a tag's meaning for a profile, falling back to the ISO set. */
function tagName(tag: string, profile: TlvProfile): string {
  return PROFILE_TAGS[profile][tag] ?? ISO_TAGS[tag] ?? ''
}

// --- Primitive value interpretation -----------------------------------------

type TagFormat = 'text' | 'lifecycle' | 'date' | 'time' | 'numeric' | 'integer'

// How well-known primitive tags should be rendered. Decoders self-validate, so
// a wrong guess on an unrelated card simply yields no interpretation.
const TAG_FORMATS: Record<string, TagFormat> = {
  '50': 'text', // application label
  '9F12': 'text', // application preferred name
  '5F20': 'text', // cardholder name
  '9F0B': 'text', // cardholder name extended
  '9F4E': 'text', // merchant name and location
  '5F50': 'text', // URL
  '5F2D': 'text', // language preference
  '5B': 'text', // OpenPGP name
  '5E': 'text', // OpenPGP login data
  '8A': 'lifecycle', // life-cycle status byte
  '5F24': 'date', // application expiration date
  '5F25': 'date', // application effective date
  '9A': 'date', // transaction date
  '9F21': 'time', // transaction time
  '5A': 'numeric', // application PAN
  '9F02': 'numeric', // amount, authorised
  '9F03': 'numeric', // amount, other
  '5F2A': 'numeric', // transaction currency code
  '5F28': 'numeric', // issuer country code
  '9F1A': 'numeric', // terminal country code
  '5F34': 'numeric', // PAN sequence number
  '9F41': 'numeric', // transaction sequence counter
  '9F36': 'integer', // application transaction counter
  '9F17': 'integer', // PIN try counter
  '9F13': 'integer' // last online ATC register
}

function decodeText(bytes: number[]): string {
  let text = ''
  for (const b of bytes) {
    if (b < 0x20 || b > 0x7e) return ''
    text += String.fromCharCode(b)
  }
  return text.trim() ? `"${text}"` : ''
}

function decodeLifecycle(bytes: number[]): string {
  if (bytes.length !== 1) return ''
  const b = bytes[0]
  if (b === 0x00) return 'No information given'
  if (b === 0x01) return 'Creation state'
  if (b === 0x03) return 'Initialisation state'
  if (b === 0x04 || b === 0x06) return 'Operational state (deactivated)'
  if (b === 0x05 || b === 0x07) return 'Operational state (activated)'
  if (b >= 0x0c && b <= 0x0f) return 'Termination state'
  return ''
}

/** Reads packed BCD digits, stopping at an 'F' pad nibble; null on a bad nibble. */
function bcdDigits(bytes: number[]): string | null {
  let digits = ''
  for (const b of bytes) {
    for (const nibble of [b >> 4, b & 0x0f]) {
      if (nibble <= 9) digits += String(nibble)
      else if (nibble === 0x0f) return digits
      else return null
    }
  }
  return digits
}

function decodeDate(bytes: number[]): string {
  if (bytes.length !== 3) return ''
  const digits = bcdDigits(bytes)
  if (!digits || digits.length !== 6) return ''
  const month = Number(digits.slice(2, 4))
  const day = Number(digits.slice(4, 6))
  if (month < 1 || month > 12 || day < 1 || day > 31) return ''
  return `20${digits.slice(0, 2)}-${digits.slice(2, 4)}-${digits.slice(4, 6)}`
}

function decodeTime(bytes: number[]): string {
  if (bytes.length !== 3) return ''
  const digits = bcdDigits(bytes)
  if (!digits || digits.length !== 6) return ''
  if (Number(digits.slice(0, 2)) > 23) return ''
  if (Number(digits.slice(2, 4)) > 59 || Number(digits.slice(4, 6)) > 59) return ''
  return `${digits.slice(0, 2)}:${digits.slice(2, 4)}:${digits.slice(4, 6)}`
}

function decodeNumeric(bytes: number[]): string {
  const digits = bcdDigits(bytes)
  if (!digits) return ''
  const trimmed = digits.replace(/^0+/, '')
  return trimmed === '' ? '0' : trimmed
}

function decodeInteger(bytes: number[]): string {
  if (bytes.length < 1 || bytes.length > 4) return ''
  let value = 0
  for (const b of bytes) value = value * 256 + b
  return String(value)
}

// --- EMV structured values --------------------------------------------------

// Bit meanings for an EMV bit field: outer index = byte, inner index 0..7 =
// bit 8 (MSB) down to bit 1. An empty string marks an RFU/unused bit.
type BitField = string[][]

const AIP_BITS: BitField = [
  [
    '',
    'SDA supported',
    'DDA supported',
    'Cardholder verification supported',
    'Terminal risk management to be performed',
    'Issuer authentication supported',
    'On-device cardholder verification supported',
    'CDA supported'
  ],
  ['', '', '', '', '', '', '', '']
]

const TVR_BITS: BitField = [
  [
    'Offline data authentication not performed',
    'SDA failed',
    'ICC data missing',
    'Card on terminal exception file',
    'DDA failed',
    'CDA failed',
    'SDA selected',
    ''
  ],
  [
    'ICC and terminal application versions differ',
    'Expired application',
    'Application not yet effective',
    'Requested service not allowed for card product',
    'New card',
    '',
    '',
    ''
  ],
  [
    'Cardholder verification was not successful',
    'Unrecognised CVM',
    'PIN Try Limit exceeded',
    'PIN pad not present or not working',
    'PIN required but not entered',
    'Online PIN entered',
    '',
    ''
  ],
  [
    'Transaction exceeds floor limit',
    'Lower consecutive offline limit exceeded',
    'Upper consecutive offline limit exceeded',
    'Transaction selected randomly for online processing',
    'Merchant forced transaction online',
    '',
    '',
    ''
  ],
  [
    'Default TDOL used',
    'Issuer authentication failed',
    'Script processing failed before final GENERATE AC',
    'Script processing failed after final GENERATE AC',
    '',
    '',
    '',
    ''
  ]
]

const AUC_BITS: BitField = [
  [
    'Valid for domestic cash transactions',
    'Valid for international cash transactions',
    'Valid for domestic goods',
    'Valid for international goods',
    'Valid for domestic services',
    'Valid for international services',
    'Valid at ATMs',
    'Valid at terminals other than ATMs'
  ],
  ['Domestic cashback allowed', 'International cashback allowed', '', '', '', '', '', '']
]

const TSI_BITS: BitField = [
  [
    'Offline data authentication was performed',
    'Cardholder verification was performed',
    'Card risk management was performed',
    'Issuer authentication was performed',
    'Terminal risk management was performed',
    'Script processing was performed',
    '',
    ''
  ],
  ['', '', '', '', '', '', '', '']
]

/** Lists the set bits of an EMV bit field as their meanings. */
function decodeBitField(bytes: number[], field: BitField): string[] {
  const out: string[] = []
  for (let i = 0; i < bytes.length && i < field.length; i += 1) {
    for (let bit = 0; bit < 8; bit += 1) {
      if ((bytes[i] & (0x80 >> bit)) !== 0) {
        const meaning = field[i][bit]
        if (meaning) out.push(meaning)
      }
    }
  }
  return out
}

/** Decodes an Application File Locator into its record ranges. */
function decodeAfl(bytes: number[]): string[] {
  if (bytes.length === 0 || bytes.length % 4 !== 0) return []
  const out: string[] = []
  for (let i = 0; i < bytes.length; i += 4) {
    const sfi = bytes[i] >> 3
    const first = bytes[i + 1]
    const last = bytes[i + 2]
    const offline = bytes[i + 3]
    let entry = `SFI ${sfi}, record ${first}`
    if (last !== first) entry += `–${last}`
    if (offline > 0) entry += ` (${offline} for offline auth)`
    out.push(entry)
  }
  return out
}

const CVM_METHODS: Record<number, string> = {
  0x00: 'Fail CVM processing',
  0x01: 'Plaintext PIN verified by ICC',
  0x02: 'Enciphered PIN verified online',
  0x03: 'Plaintext PIN by ICC and signature',
  0x04: 'Enciphered PIN verified by ICC',
  0x05: 'Enciphered PIN by ICC and signature',
  0x1e: 'Signature',
  0x1f: 'No CVM required'
}

const CVM_CONDITIONS: Record<number, string> = {
  0x00: 'always',
  0x01: 'if unattended cash',
  0x02: 'if not unattended/manual cash or cashback',
  0x03: 'if terminal supports the CVM',
  0x04: 'if manual cash',
  0x05: 'if purchase with cashback',
  0x06: 'if under the X value',
  0x07: 'if over the X value',
  0x08: 'if under the Y value',
  0x09: 'if over the Y value'
}

/** Decodes a CVM List into its cardholder verification rules. */
function decodeCvmList(bytes: number[]): string[] {
  if (bytes.length < 10 || bytes.length % 2 !== 0) return []
  const out: string[] = []
  for (let i = 8; i + 1 < bytes.length; i += 2) {
    const code = bytes[i]
    const condition = bytes[i + 1]
    const method = CVM_METHODS[code & 0x3f] ?? `CVM 0x${byteHex(code & 0x3f)}`
    const when = CVM_CONDITIONS[condition] ?? `condition 0x${byteHex(condition)}`
    const fallthrough = (code & 0x40) !== 0 ? ', else next rule' : ''
    out.push(`${method} — ${when}${fallthrough}`)
  }
  return out
}

/** Decodes the EMV structured / bit-field tags, or [] when the tag is not one. */
function decodeEmvStructured(tag: string, bytes: number[]): string[] {
  switch (tag) {
    case '82':
      return decodeBitField(bytes, AIP_BITS)
    case '95':
      return decodeBitField(bytes, TVR_BITS)
    case '9F07':
      return decodeBitField(bytes, AUC_BITS)
    case '9B':
      return decodeBitField(bytes, TSI_BITS)
    case '94':
      return decodeAfl(bytes)
    case '8E':
      return decodeCvmList(bytes)
    default:
      return []
  }
}

/** Renders a known primitive tag's value as human-readable lines, or []. */
function decodeValue(tag: string, bytes: number[], profile: TlvProfile): string[] {
  if (bytes.length === 0) return []

  // EMV structured tags (e.g. tag 82) collide with ISO meanings, so gate them.
  if (profile === 'emv') {
    const structured = decodeEmvStructured(tag, bytes)
    if (structured.length > 0) return structured
  }

  let scalar = ''
  switch (TAG_FORMATS[tag]) {
    case 'text':
      scalar = decodeText(bytes)
      break
    case 'lifecycle':
      scalar = decodeLifecycle(bytes)
      break
    case 'date':
      scalar = decodeDate(bytes)
      break
    case 'time':
      scalar = decodeTime(bytes)
      break
    case 'numeric':
      scalar = decodeNumeric(bytes)
      break
    case 'integer':
      scalar = decodeInteger(bytes)
      break
  }
  return scalar ? [scalar] : []
}

// Known application-identifier prefixes, longest/most-specific first.
const AID_PROFILES: { prefix: string; profile: TlvProfile }[] = [
  { prefix: 'D27600012401', profile: 'openpgp' },
  { prefix: 'A000000308', profile: 'piv' },
  { prefix: 'A000000003', profile: 'emv' }, // Visa
  { prefix: 'A000000004', profile: 'emv' }, // Mastercard
  { prefix: 'A000000005', profile: 'emv' }, // Mastercard (legacy)
  { prefix: 'A000000025', profile: 'emv' }, // American Express
  { prefix: 'A000000065', profile: 'emv' }, // JCB
  { prefix: 'A000000152', profile: 'emv' }, // Discover
  { prefix: 'A000000333', profile: 'emv' } // UnionPay
]

/** Infers an application profile from a SELECT AID, or null when unknown. */
export function detectProfile(aid: string): TlvProfile | null {
  const upper = aid.replace(/[^0-9a-fA-F]/g, '').toUpperCase()
  for (const { prefix, profile } of AID_PROFILES) {
    if (upper.startsWith(prefix)) return profile
  }
  return null
}

function spacedHex(bytes: number[]): string {
  return bytes.map(byteHex).join(' ')
}

function parseTag(bytes: number[], pos: number): { tag: number[]; next: number } | null {
  if (pos >= bytes.length) return null
  const tag = [bytes[pos]]
  let next = pos + 1

  if ((bytes[pos] & 0x1f) === 0x1f) {
    let more = true
    while (more) {
      if (next >= bytes.length) return null
      const b = bytes[next]
      tag.push(b)
      next += 1
      more = (b & 0x80) === 0x80
    }
  }

  return { tag, next }
}

function parseLength(bytes: number[], pos: number): { length: number; next: number } | null {
  if (pos >= bytes.length) return null
  const first = bytes[pos]

  if (first < 0x80) return { length: first, next: pos + 1 }
  if (first === 0x80) return null // indefinite length is not used on smart cards

  const count = first & 0x7f
  if (count > 4) return null

  let length = 0
  let next = pos + 1
  for (let i = 0; i < count; i += 1) {
    if (next >= bytes.length) return null
    length = (length << 8) | bytes[next]
    next += 1
  }
  return { length, next }
}

function parseNodes(
  bytes: number[],
  start: number,
  end: number,
  depth: number,
  profile: TlvProfile
): TlvNode[] | null {
  if (depth > 16) return null

  const nodes: TlvNode[] = []
  let pos = start

  while (pos < end) {
    const tagResult = parseTag(bytes, pos)
    if (!tagResult) return null
    const lengthResult = parseLength(bytes, tagResult.next)
    if (!lengthResult) return null

    const valueStart = lengthResult.next
    const valueEnd = valueStart + lengthResult.length
    if (valueEnd > end) return null

    const constructed = (bytes[pos] & 0x20) === 0x20
    let children: TlvNode[] = []
    if (constructed) {
      const parsed = parseNodes(bytes, valueStart, valueEnd, depth + 1, profile)
      if (!parsed) return null
      children = parsed
    }

    const tag = tagResult.tag.map(byteHex).join('')
    const valueBytes = bytes.slice(valueStart, valueEnd)
    nodes.push({
      tag,
      name: tagName(tag, profile),
      constructed,
      length: lengthResult.length,
      value: spacedHex(valueBytes),
      decoded: constructed ? [] : decodeValue(tag, valueBytes, profile),
      children
    })
    pos = valueEnd
  }

  return nodes
}

/**
 * Parses the hex string as a sequence of BER-TLV objects, naming tags using
 * the given application profile. `ok` is false when the bytes are not
 * well-formed TLV — the caller should then fall back to a plain hex display.
 */
export function parseTlv(hex: string, profile: TlvProfile = 'iso'): TlvResult {
  const bytes = toBytes(hex)
  if (bytes.length === 0) return { ok: false, nodes: [] }

  const nodes = parseNodes(bytes, 0, bytes.length, 0, profile)
  if (!nodes || nodes.length === 0) return { ok: false, nodes: [] }
  return { ok: true, nodes }
}
